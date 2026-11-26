#include "main/game/techniques.h"

#include <gf/gf_heap_manager.h>
#include <gf/gf_memory_pool.h>
#include <math.h>
#include <memory.h>
#include <so/so_module_accesser.h>
#include <so/work/so_work_manage_module_impl.h>
#include <string.h>

#include "main/game/fighters.h"
#include "main/game/status_ids.h"
#include "main/game/tracker.h"
#include "shared/game/heap.h"
#include "shared/game/mem.h"
#include "shared/log.h"

// Technique timings worked out from the trackers' frames and passed to the
// sink; changes no game state. What each technique is and how it is timed:
// docs/internals.md, "Technique timings". Wavedash adapted from ProjectPunch's
// wavedash_info.cpp, the ledgedash cells and drop-off rule from its
// ledge_tech.cpp (MIT; see THIRD_PARTY_NOTICES.md).
namespace tm {
namespace game {
    enum {
        kStatusCliffCatchStart = 0x73,
        kStatusCliffCatch = 0x74,
        kStatusCliffWait = 0x75,
        kStatusCliffJumpStart = 0x79,
        kStatusCliffJumpEnd = 0x7A,
        kStatusAttackHi4Start = 0x30,
        kStatusGuardDamageId = kStatusGuardDamage,
        kStatusCatchDash = 0x36,
        kStatusPassive = 0x60,
        kStatusPassiveCeil = 0x64,
        kStatusDamageLast = 0x5F,
        kStatusPass = 0x72,
        kStatusAttackDash = 0x26,
        kStatusEscapeF = 0x1F,
        kStatusItemThrow = 0x9B,
        kStatusCapturePulled = 0x3D,
        kStatusCaptureDamage = 0x3F,
        kStatusDown = 0x4A,
        kStatusDownStand = 0x51,
        kStatusDownStandAttack = 0x53,
        kStatusPassiveFb = 0x61,
        // P+ Constants.asm:121-122: powershield window (23078) 3, which is 2
        // frames (observed: a hit on the shield's 1st or 2nd frame); tech
        // window (23052) 20 frames. Early misses report up to 8 frames past
        // the window (10 non-hitlag shield frames).
        kPowershieldFrames = 2,
        kPowershieldEarlyMax = 10,
        kReflectBit = 0x2200000A, // RA-Bit[10]: P+ RSBE01.TXT:1266-1281, PowerShield.asm:97-104
        kTechWindow = 20,
        // Observed on the Fox CPU; not found in P+'s source.
        kTechLockout = 41,
        kGiveUp = 60,         // frames an Act Oo* timing waits for the action
        kActOoLagShown = 15,  // 0.25 s cap for reporting post-lag actions
        kLCancelShown = 30,   // 0.5 s pre-landing L-cancel press window
        kLedgeGiveUp = 120,   // frames from leaving the ledge to landing
        kWavedashAir = 30,    // airborne frames before the airdodge at most
        kCliffwaitShown = 24, // 0.4 s cap for reporting ledge-hang duration
        kFallShown = 10,      // Act OoAirborne: frames into a fall
        kPeakShown = 5,       // and frames past a jump's peak
        // ProjectPunch's ledge_tech.cpp: dropping off is possible from the
        // 3rd CliffWait frame, the ledge options from the 1st.
        kEarliestDrop = 3,
        kEarliestOption = 1,
        kLedgeGrabsInt = 79,    // LA-Basic[79] (0x1000004F): P+ Ledge.asm:495-513
        kLedgeGrabLimit = 5,
        kShortHopFlag = 1 << 6, // status work flag word 0
        kGroundActionGroups = (1 << kGroupGroundSpecial) | (1 << kGroupGroundCatch) | (1 << kGroupGroundAttack)
                              | (1 << kGroupGroundEscape) | (1 << kGroupGroundGuard) | (1 << kGroupGroundJump)
    };

    struct Watch {
        bool valid;
        Body body;
        Body prevBody;
        float damage;
        float prevDamage;
        // Wavedash.
        s32 jumpSquat;
        s32 air;
        s32 airdodge;
        u8 jumpHeld;
        u8 shortHop;
        float angle;
        // Jump cancel.
        s32 jcSquat;
        // L-cancel.
        bool lcPressed;
        bool lcMissed;
        s16 lcHits, lcLandings;
        // Act out of shield / lag / hitstun.
        bool oosArmed;
        s32 oosStart;
        bool lagArmed;
        bool lagAutocancel;
        s16 lagSource; // status the count is out of
        s32 lagFirst;  // first actionable frame after it
        s32 lagArmedAt;
        bool hsArmed;
        s32 hsStart;
        s32 hsArmedAt;
        // Fastfall.
        s32 fallStart;
        bool ffDone;
        // Act out of airborne: what the fighter is in (kAir*), since when,
        // and the first frame it stopped rising.
        u8 airKind;
        s32 airSince;
        s32 airPeak;
        // Dashback: in a slow turn from standing, which way, since when.
        bool dbTurn;
        bool dbLeft;
        s32 dbSince;
        s16 dbHits, dbTries;
        // Powershield: armed until the first hit/reflect of this shield, and
        // non-hitlag frames spent in GuardOn/Guard (1 = GuardOn's first frame).
        bool guardArmed;
        u16 guardFrames;
        // Tech lockout: the last shield press, whether the lockout refused
        // it and how many lockout frames were left then.
        s32 press;
        bool pressRefused;
        s16 pressLeft;
        // Boost grab: the frame the dash attack began; item throws: the
        // frame the roll or airdodge began.
        s32 dashAttack;
        s32 dodge;
        // Grab breakout: grabbed since the frame before; the timer then.
        bool grabbed;
        u16 grabFrames;
        float grabStart;
        // Combo (as the defender).
        u8 comboHits;
        s8 comboAttacker;
        bool comboBroken;
        float comboStart;
        // SDI (as the defender).
        bool sdiOn;
        u8 sdiInputs, sdiShifts;
        s8 sdiRegion;
        // Ledge.
        u8 ledgePhase; // 0 off, 1 on the ledge, 2 left it
        bool ledgeAir;
        s32 ledgeLeft;
        s32 lastIntangible;
        bool hangXlu; // false: CliffWait started with no intangibility and got none
        s16 waitFrames;
        u8 ledgeGrabs; // P+ LA-Basic[79] consecutive ledge-grab counter on this hang
        u8 adHas;
        float adAngle;
        u16 cellCount;
        u8 cells[kLedgeCells];
    };

    struct TechniqueNet {
        Watch watch[kMaxTracked];
        TechniqueState state[kMaxTracked];
    };

    static TechniqueNet* s_net = NULL;
    static Ledgedash s_ledge;      // human player only (Ledgedash HUD)
    static Ledgedash s_ledgeEvent; // last kTechGalint emitter (human or CPU)
    static TechniqueSink s_sink = NULL;

    static void clearWatch(Watch& w)
    {
        memset(&w, 0, sizeof(w));
        w.jumpSquat = w.air = w.airdodge = kNoFrame;
        w.jcSquat = kNoFrame;
        w.oosStart = w.lagFirst = w.hsStart = kNoFrame;
        w.fallStart = kNoFrame;
        w.airSince = w.airPeak = kNoFrame;
        w.dbSince = w.press = w.dashAttack = w.dodge = kNoFrame;
        w.comboAttacker = -1;
        w.comboBroken = true;
        w.sdiRegion = -1;
        w.ledgeLeft = kNoFrame;
        w.lastIntangible = kNoFrame;
    }

    void techniquesAttach()
    {
        if (s_net == NULL)
        {
            u32 need = (static_cast<u32>(sizeof(TechniqueNet)) + 31u) & ~31u;
            HeapUsage u;
            if (heapUsage(Heaps::Network, u) && u.largest >= need)
            {
                void* p = gfMemoryPool::alloc(gfHeapManager::getHeap(Heaps::Network), need, 32);
                if (isRam(p))
                {
                    memset(p, 0, need);
                    s_net = static_cast<TechniqueNet*>(p);
                }
            }
            if (s_net == NULL)
            {
                TM_LOG("techniques: alloc failed (%u bytes in Network)\n", static_cast<unsigned>(need));
            }
        }
    }

    void techniquesDetach()
    {
        if (s_net != NULL)
        {
            free(s_net);
            s_net = NULL;
        }
        s_sink = NULL;
    }

    void techniquesReset(TechniqueSink sink)
    {
        s_sink = sink;
        if (s_net != NULL)
        {
            for (int i = 0; i < kMaxTracked; i++)
            {
                clearWatch(s_net->watch[i]);
                s_net->state[i].valid = false;
            }
        }
        s_ledge.done = kNoFrame;
        s_ledge.count = 0;
        s_ledgeEvent.done = kNoFrame;
        s_ledgeEvent.count = 0;
    }

    const TechniqueState* techniqueState(int i)
    {
        return s_net != NULL && i >= 0 && i < kMaxTracked && s_net->state[i].valid ? &s_net->state[i] : NULL;
    }

    const Ledgedash& lastLedgedash()
    {
        return s_ledge;
    }

    const Ledgedash& lastLedgedashEvent()
    {
        return s_ledgeEvent;
    }

#ifdef __MWERKS__
#pragma dont_inline on
#endif
    static void emit(int kind, const Tracked& t, int tracked, int frame, int flag, int a, int b, float angle)
    {
        if (s_sink == NULL)
        {
            return;
        }
        Technique m;
        m.kind = static_cast<u8>(kind);
        m.player = t.playerNo;
        m.tracked = static_cast<s8>(tracked);
        m.flag = static_cast<u8>(flag);
        m.frame = static_cast<s16>(frame);
        m.a = static_cast<s16>(a);
        m.b = static_cast<s16>(b);
        m.angle = angle;
        s_sink(m);
    }

    static bool landingStatus(int s)
    {
        return s >= kStatusLanding && s <= kStatusLandingFallSpecial;
    }

    static bool guardStatus(int s)
    {
        return s == kStatusGuardOn || s == kStatusGuard || s == kStatusGuardDamageId;
    }

    // Statuses only a choice enters: moving, jumping, shielding, dodging,
    // attacking, grabbing, specials.
    static bool actionStatus(int s)
    {
        return s == kStatusWalk || s == kStatusDash || s == kStatusTurnDash || s == kStatusJumpSquat
               || s == kStatusJumpAerial || s == kStatusFly || s == kStatusSquat || s == kStatusGuardOn
               || s == kStatusGuardOff || (s >= kStatusEscape && s <= kStatusEscapeAir)
               || (s >= kStatusAttack && s <= kStatusCatchDash) || (s >= kStatusSpecialN && s < kStatusFinal);
    }

    // Post-lag action sources in P+ statuses: aerial landing, autocancelled
    // aerial (Landing straight out of AttackAir), special/airdodge landing,
    // throw, getup, and ground tech (in place or roll). Plain landings are
    // excluded so normal movement does not spam the OSD.
    static bool lagSource(int s, int prev)
    {
        return s == kStatusLandingAttackAir || (s == kStatusLanding && prev == kStatusAttackAirFirst)
               || s == kStatusLandingFallSpecial || s == kStatusThrow
               || (s >= kStatusDownStand && s <= kStatusDownStandAttack)
               || s == kStatusPassive || s == kStatusPassiveFb;
    }

    // Transitional idle/movement states counted through on the way to the
    // chosen action.
    static bool lagLookThrough(int s)
    {
        return s == kStatusWait || s == kStatusWalk || s == kStatusWalkBrake || s == kStatusTurn
               || s == kStatusSquat || s == kStatusSquatWait || s == kStatusSquatRv;
    }

    // Shielding after lag is excluded to avoid noise during defensive play.
    static bool lagAction(int s)
    {
        return s != kStatusGuardOn && (actionStatus(s) || s == kStatusPass);
    }

    // Degrees below horizontal of a stick position.
    static float stickAngle(float x, float y)
    {
        float ax = x < 0.0f ? -x : x;
        float ay = y < 0.0f ? -y : y;
        if (ax < 0.0001f && ay < 0.0001f)
        {
            return 0.0f;
        }
        return static_cast<float>(atan2(ay, ax) * 57.29577951308232);
    }

    // One of 8 directions, or -1 inside the dead zone.
    static int stickRegion(float x, float y)
    {
        if (x * x + y * y < 0.49f)
        {
            return -1;
        }
        float a = static_cast<float>(atan2(y, x) * 57.29577951308232) + 360.0f + 22.5f;
        return static_cast<int>(a / 45.0f) % 8;
    }

    static u8 cellOf(int s)
    {
        return ledgeCellOf(s);
    }
#ifdef __MWERKS__
#pragma dont_inline reset
#endif

    static void wavedash(Watch& w, const Tracked& t, int i, s32 f)
    {
        for (int k = 0; k < t.eventCount; k++)
        {
            const StatusEvent& e = t.events[k];
            int s = e.status;
            if (s == kStatusJumpSquat)
            {
                w.jumpSquat = f;
                w.air = w.airdodge = kNoFrame;
                w.jumpHeld = 0;
            }
            else if (s == kStatusJump && e.prev == kStatusJumpSquat && w.jumpSquat != kNoFrame)
            {
                w.air = f;
                w.shortHop = (e.flags & kShortHopFlag) != 0 ? 1 : 0;
            }
            else if (s == kStatusEscapeAir && w.air != kNoFrame && f - w.air < kWavedashAir)
            {
                w.airdodge = f;
                w.angle = stickAngle(w.body.stickX, w.body.stickY);
            }
            else if (s == kStatusLandingFallSpecial && e.prev == kStatusEscapeAir && w.airdodge != kNoFrame)
            {
                emit(kTechWavedash, t, i, w.airdodge - w.air + 1, w.shortHop, w.jumpHeld, 0, w.angle);
                w.jumpSquat = w.air = w.airdodge = kNoFrame;
            }
            else if (s != kStatusJump && s != kStatusEscapeAir)
            {
                w.jumpSquat = w.air = w.airdodge = kNoFrame;
            }
        }
        if (t.cur.status == kStatusJumpSquat && w.jumpSquat != kNoFrame && (w.body.buttons & kButtonJump)
            && w.jumpHeld == f - w.jumpSquat)
        {
            w.jumpHeld++;
        }
    }

    static void lCancel(Watch& w, const Tracked& t, int i)
    {
        // In an aerial at the start of the frame, or since entering one in it.
        bool aerial = t.prev.status == kStatusAttackAirFirst;
        bool started = false;
        for (int k = 0; k < t.eventCount; k++)
        {
            const StatusEvent& e = t.events[k];
            if (e.status == kStatusAttackAirFirst)
            {
                aerial = true;
                started = true;
                w.lcPressed = false;
            }
            else if (e.status == kStatusLandingAttackAir && e.prev == kStatusAttackAirFirst && aerial)
            {
                // P+ L-Cancel.asm:64,97,105,145,194: LA-Basic[90] is set to 7 on
                // a press in initStatus/execStatus, 8 in execStop (hitlag), and
                // decremented only in execStatus (not in hitlag), so 8 - counter
                // is the P+ window frame at landing.
                if (e.lCancel >= 7 || (started && e.lCancel > 0))
                {
                    w.lcPressed = true;
                }
                bool ok = e.lCancel > 0;
                w.lcLandings++;
                w.lcHits += ok ? 1 : 0;
                w.lcMissed = !ok;
                int frame = kNoValue;
                if (w.lcPressed)
                {
                    frame = 8 - e.lCancel;
                    if (frame < 1)
                    {
                        frame = 1;
                    }
                    else if (frame > kLCancelShown)
                    {
                        frame = kNoValue;
                    }
                }
                emit(kTechLCancel, t, i, frame, ok, w.lcHits, w.lcLandings, 0.0f);
                w.lcPressed = false;
                aerial = false;
            }
            else
            {
                aerial = false;
            }
        }
        if (aerial && t.cur.status == kStatusAttackAirFirst
            && (w.body.lCancel >= 7 || (started && w.body.lCancel > 0)))
        {
            w.lcPressed = true;
        }
        if (t.cur.status != kStatusLandingAttackAir)
        {
            w.lcMissed = false;
        }
    }

    static void actOutOf(Watch& w, const Tracked& t, int i, s32 f)
    {
        bool hit = t.cur.hitstun > 0 || t.cur.hitlag > 0;
        // Out of shield.
        for (int k = 0; k < t.eventCount; k++)
        {
            const StatusEvent& e = t.events[k];
            if (e.status == kStatusGuardDamageId)
            {
                w.oosArmed = true;
                w.oosStart = kNoFrame;
                continue;
            }
            if (!w.oosArmed)
            {
                continue;
            }
            if (e.prev == kStatusGuardDamageId && w.oosStart == kNoFrame)
            {
                w.oosStart = f;
            }
            // Shield drop is not the action: keep counting through GuardOff so
            // the action taken out of shield is reported. P+'s GuardOff ends
            // into Wait and an attack can take over Wait in the same frame
            // (observed on Mario: Guard_Off -> Wait -> forward smash in one
            // frame), so that transient Wait is passed over.
            bool passing = e.status == kStatusWait && e.prev == kStatusGuardOff && k + 1 < t.eventCount;
            if (!guardStatus(e.status) && e.status != kStatusGuardOff && !passing)
            {
                if (actionStatus(e.status) && !hit && w.oosStart != kNoFrame)
                {
                    emit(kTechActOos, t, i, f - w.oosStart + 1, 0, 0, 0, 0.0f);
                }
                w.oosArmed = false;
            }
        }
        if (w.oosArmed && w.oosStart != kNoFrame && f - w.oosStart > kGiveUp)
        {
            w.oosArmed = false;
        }
        // Out of landing lag.
        if (hit)
        {
            w.lagArmed = false;
        }
        for (int k = 0; k < t.eventCount && !hit; k++)
        {
            const StatusEvent& e = t.events[k];
            if (lagSource(e.status, e.prev))
            {
                w.lagArmed = true;
                w.lagAutocancel = e.status == kStatusLanding;
                w.lagSource = e.status;
                w.lagFirst = kNoFrame;
                w.lagArmedAt = f;
                continue;
            }
            if (!w.lagArmed)
            {
                continue;
            }
            // Wait counts only straight out of a lag source (never after an
            // autocancelled Landing).
            bool waitOk = e.status != kStatusWait || (!w.lagAutocancel && e.prev == w.lagSource);
            bool through = lagLookThrough(e.status) && waitOk;
            bool action = !lagLookThrough(e.status) && lagAction(e.status);
            if ((through || action) && w.lagFirst == kNoFrame)
            {
                w.lagFirst = f;
            }
            if (action && f - w.lagFirst + 1 <= kActOoLagShown)
            {
                emit(kTechActOoLag, t, i, f - w.lagFirst + 1, w.lagAutocancel, 0, 0, 0.0f);
            }
            if (!through)
            {
                w.lagArmed = false;
            }
        }
        // Frame 1 is the first frame the fighter can act after the source,
        // either inside it (Landing opens interrupts before it ends; aerial
        // landing opens them when its lag count finishes:
        // ftStatusUniqProcessLanding, 0x808746B8) or in Wait. On the frame the
        // source starts, only its own interrupts count.
        if (w.lagArmed && w.lagFirst == kNoFrame && !hit
            && (w.lagArmedAt != f ? t.cur.actionable != 0
                                  : (t.cur.groups & kGroundActionGroups) != 0 || t.cur.cancel != 0))
        {
            w.lagFirst = f;
        }
        if (w.lagArmed
            && (w.lagFirst != kNoFrame ? f - w.lagFirst + 1 > kActOoLagShown : f - w.lagArmedAt > kGiveUp + 40))
        {
            w.lagArmed = false;
        }
        // Out of hitstun.
        if (t.cur.hitstun > 0)
        {
            w.hsArmed = true;
            w.hsStart = kNoFrame;
            w.hsArmedAt = f;
            return;
        }
        if (!w.hsArmed)
        {
            return;
        }
        if (w.hsStart == kNoFrame && t.cur.actionable && t.cur.hitlag == 0)
        {
            w.hsStart = f;
        }
        for (int k = 0; k < t.eventCount && w.hsArmed; k++)
        {
            int s = t.events[k].status;
            if (actionStatus(s))
            {
                emit(kTechActOoHitstun, t, i, f - (w.hsStart != kNoFrame ? w.hsStart : f) + 1, 0, 0, 0, 0.0f);
                w.hsArmed = false;
            }
            else if ((s >= kStatusPassive && s <= kStatusPassiveCeil) || s == kStatusCliffCatchStart
                     || s == kStatusCliffCatch)
            {
                // A tech or a ledge grab ends it.
                w.hsArmed = false;
            }
        }
        if (w.hsArmed && ((w.hsStart != kNoFrame && f - w.hsStart > kGiveUp) || f - w.hsArmedAt > kGiveUp + 60))
        {
            w.hsArmed = false;
        }
    }

    static void jumpCancel(Watch& w, const Tracked& t, int i, s32 f)
    {
        for (int k = 0; k < t.eventCount; k++)
        {
            const StatusEvent& e = t.events[k];
            int s = e.status;
            if (s == kStatusJumpSquat)
            {
                w.jcSquat = f;
            }
            else if (e.prev == kStatusJumpSquat && w.jcSquat != kNoFrame
                     && (s == kStatusCatch || s == kStatusAttackHi4Start || (s >= kStatusSpecialN && s < kStatusFinal)))
            {
                int frame = f - w.jcSquat;
                emit(kTechJumpCancel, t, i, frame < 1 ? 1 : frame, 0, 0, 0, 0.0f);
                w.jcSquat = kNoFrame;
            }
            else
            {
                w.jcSquat = kNoFrame;
            }
        }
    }

    static void fastfall(Watch& w, const Tracked& t, int i, s32 f)
    {
        int s = t.cur.status;
        TechniqueState& st = s_net->state[i];
        if (!t.cur.airborne || t.cur.hitstun > 0)
        {
            w.fallStart = kNoFrame;
            w.ffDone = !t.cur.airborne ? false : w.ffDone;
            st.canFastfall = false;
            return;
        }
        // The window first: a fastfall on its first frame is frame 1.
        if (w.body.vy > 0.0f)
        {
            w.fallStart = kNoFrame;
        }
        else if (w.fallStart == kNoFrame && t.cur.hitlag == 0)
        {
            w.fallStart = f;
        }
        if (w.body.fastfall && !w.prevBody.fastfall && !w.ffDone)
        {
            if (w.fallStart != kNoFrame)
            {
                emit(kTechFastfall, t, i, f - w.fallStart + 1, 0, 0, 0, 0.0f);
            }
            w.ffDone = true;
        }
        bool falling = s == kStatusJump || s == kStatusJumpAerial || s == kStatusFly || s == kStatusFall
                       || s == kStatusFallAerial || s == kStatusAttackAirFirst;
        st.canFastfall = falling && w.body.vy <= 0.0f && !w.body.fastfall && !w.ffDone && t.cur.hitlag == 0;
    }

    // Exclude falls entered from jumps, aerials, airdodges, specials, or hitstun.
    static bool fallCounts(int prev)
    {
        return prev != kStatusJump && prev != kStatusAttackAirFirst && prev != kStatusEscapeAir
               && prev < kStatusSpecialN && (prev < kStatusDamage || prev > kStatusDamageLast);
    }

    static void airborne(Watch& w, const Tracked& t, int i, s32 f)
    {
        if (w.airKind != kAirNone && w.airPeak == kNoFrame && w.body.vy <= 0.0f)
        {
            w.airPeak = f;
        }
        for (int k = 0; k < t.eventCount; k++)
        {
            const StatusEvent& e = t.events[k];
            int s = e.status;
            u8 kind = kAirNone;
            if (s == kStatusJump && e.prev == kStatusJumpSquat)
            {
                kind = kAirJump;
            }
            else if (s == kStatusJumpAerial || s == kStatusFly)
            {
                if (w.airKind == kAirJump)
                {
                    emit(kTechActOoAirborne, t, i, f - w.airSince + 1, kAirJump | kAirToJump, 0, 0, 0.0f);
                }
                kind = kAirDoubleJump;
            }
            else if ((s == kStatusFall || s == kStatusFallAerial) && fallCounts(e.prev))
            {
                kind = kAirFall;
            }
            else if (s == kStatusPass)
            {
                kind = kAirDrop;
            }
            else if (w.airKind != kAirNone && (s == kStatusAttackAirFirst || (s >= kStatusSpecialN && s < kStatusFinal)))
            {
                emit(kTechActOoAirborne, t, i, f - w.airSince + 1, w.airKind | (s == kStatusAttackAirFirst ? 0 : kAirSpecial),
                     0, 0, 0.0f);
            }
            w.airKind = kind;
            // Frame 1 is this frame for a jump out of its squat (and a walk
            // off an edge, unchecked); the next for a double jump, a
            // platform drop or a ledge drop, which are the frame's action.
            w.airSince = kind == kAirDoubleJump || kind == kAirDrop || e.prev == kStatusCliffWait ? f + 1 : f;
            w.airPeak = kNoFrame;
        }
        // Display window cut-offs, applied to the next frame.
        if ((w.airKind == kAirFall && f - w.airSince + 2 > kFallShown)
            || ((w.airKind == kAirJump || w.airKind == kAirDoubleJump) && w.airPeak != kNoFrame
                && f - w.airPeak >= kPeakShown))
        {
            w.airKind = kAirNone;
        }
    }

    // P+'s dash sensitivity (Constants.asm, 3122).
    static const float kDashStick = 0.73f;

    static void dashback(Watch& w, const Tracked& t, int i, int how)
    {
        w.dbTurn = false;
        w.dbTries++;
        w.dbHits = static_cast<s16>(w.dbHits + (how != kDashbackFailed ? 1 : 0));
        emit(kTechDashback, t, i, kNoValue, how, w.dbHits, w.dbTries, 0.0f);
    }

    static bool shieldReflected(Fighter* fighter)
    {
        const soModuleEnumeration* m = fighterModules(fighter);
        return m != NULL && isRam(m->m_workManageModule) && m->m_workManageModule->isFlag(kReflectBit);
    }

    // Techniques that follow statuses and the shield button: dashback,
    // powershield, tech lockout, item throws out of a roll or airdodge,
    // boost grab, grab breakout.
    static void ground(Watch& w, const Tracked& t, int i, s32 f)
    {
        // P+ RSBE01.TXT:793-796: the powershield window does not decrement in
        // hitlag (execStop runs when hitlag is nonzero before and after).
        if (w.guardArmed && !(t.prev.hitlag > 0 && t.cur.hitlag > 0))
        {
            w.guardFrames++;
        }
        const Body& b = w.body;
        if ((b.buttons & kButtonGuard) != 0 && (w.prevBody.buttons & kButtonGuard) == 0)
        {
            s32 since = f - w.press;
            w.pressRefused = w.press != kNoFrame && since < kTechLockout;
            w.pressLeft = static_cast<s16>(kTechLockout - since);
            w.press = f;
        }
        for (int k = 0; k < t.eventCount; k++)
        {
            const StatusEvent& e = t.events[k];
            int s = e.status;
            int prev = e.prev;
            bool standing = prev <= kStatusWalkBrake || landingStatus(prev);
            if (s == kStatusTurnDash && standing)
            {
                dashback(w, t, i, kDashbackDirect);
            }
            else if (s == kStatusTurn && standing)
            {
                w.dbTurn = true;
                w.dbSince = f;
                w.dbLeft = b.stickX < 0.0f;
            }
            else if (w.dbTurn && prev == kStatusTurn)
            {
                if (s == kStatusDash || s == kStatusTurnDash)
                {
                    dashback(w, t, i, kDashbackSlowTurn);
                }
                w.dbTurn = false;
            }
            if (s == kStatusGuardOn)
            {
                w.guardArmed = true;
                w.guardFrames = 1;
            }
            else if (s == kStatusGuardDamageId && (prev == kStatusGuardOn || prev == kStatusGuard) && w.guardArmed)
            {
                int n = w.guardFrames;
                if (n <= kPowershieldEarlyMax)
                {
                    int how = b.powershield ? kPowershieldDone : n <= kPowershieldFrames ? kPowershieldBuffered
                                                                                         : kPowershieldEarly;
                    emit(kTechPowershield, t, i, how == kPowershieldEarly ? n - kPowershieldFrames : n, how, 0, 0,
                         0.0f);
                }
                w.guardArmed = false;
            }
            else if (w.guardArmed && s != kStatusGuard)
            {
                w.guardArmed = false;
            }
            if (s >= kStatusEscapeF && s <= kStatusEscapeAir)
            {
                w.dodge = f;
            }
            else if (s == kStatusItemThrow && prev >= kStatusEscapeF && prev <= kStatusEscapeAir && w.dodge != kNoFrame)
            {
                emit(kTechItemThrow, t, i, f - w.dodge + 1, prev == kStatusEscapeAir, 0, 0, 0.0f);
            }
            if (s == kStatusAttackDash)
            {
                w.dashAttack = f;
            }
            else if (prev == kStatusAttackDash && w.dashAttack != kNoFrame
                     && (s == kStatusCatchDash || s == kStatusAttackHi4Start))
            {
                emit(kTechBoostGrab, t, i, f - w.dashAttack + 1, s == kStatusAttackHi4Start, 0, 0, 0.0f);
            }
            if (s == kStatusDown && w.pressRefused && f - w.press < kTechWindow)
            {
                emit(kTechTechLockout, t, i, w.pressLeft, 0, 0, 0, 0.0f);
            }
        }
        // P+ RSBE01.TXT:1266-1281, PowerShield.asm:97-104: a powershielded
        // projectile reflects (RA-Bit[10]) without entering GuardDamage.
        if (w.guardArmed)
        {
            if ((t.cur.status == kStatusGuardOn || t.cur.status == kStatusGuard) && shieldReflected(t.fighter))
            {
                emit(kTechPowershield, t, i, w.guardFrames, kPowershieldDone, 0, 0, 0.0f);
                w.guardArmed = false;
            }
            else if (t.cur.status != kStatusGuardOn && t.cur.status != kStatusGuard)
            {
                w.guardArmed = false;
            }
        }
        // A slow turn missed the dashback if the stick reaches the dash
        // sensitivity after the turn's second frame, while it still turns.
        if (w.dbTurn && t.cur.status == kStatusTurn && f - w.dbSince >= 2
            && (w.dbLeft ? -b.stickX : b.stickX) >= kDashStick)
        {
            dashback(w, t, i, kDashbackFailed);
        }
        // Standard grabs (0x3D..0x3F) and Yoshi tongue grabs (0x107..0x109).
        bool inGrabHold = (t.cur.status >= kStatusCapturePulled && t.cur.status <= kStatusCaptureDamage)
                          || (t.cur.status >= 0x107 && t.cur.status <= 0x109);
        if (!inGrabHold)
        {
            if (w.grabbed)
            {
                bool escaped = t.cur.status == 0x40 || t.cur.status == 0x41 || t.cur.status == 0x10A;
                if (escaped && w.grabFrames > 0 && w.grabStart > 0.0f)
                {
                    int optimal = static_cast<int>((w.grabStart + 16.999f) / 17.0f);
                    if (optimal < 1)
                    {
                        optimal = 1;
                    }
                    emit(kTechGrabBreakout, t, i, static_cast<int>(w.grabFrames), 1, optimal, 0,
                         w.grabStart / static_cast<float>(w.grabFrames));
                }
            }
            w.grabbed = false;
            return;
        }
        if (!w.grabbed)
        {
            w.grabbed = true;
            w.grabFrames = 0;
            w.grabStart = b.clatter;
        }
        w.grabFrames++;
        emit(kTechGrabBreakout, t, i, kNoValue, 0, static_cast<int>(b.clatter + 0.5f), 0,
             (w.grabStart - b.clatter) / w.grabFrames);
    }

    static void combo(Watch& w, const Tracked& t, int i)
    {
        if (t.cur.actionable && t.cur.hitstun == 0 && t.cur.hitlag == 0)
        {
            w.comboBroken = true;
        }
        if (!t.cur.hitReceived || t.cur.shieldReceived)
        {
            return;
        }
        int attacker = t.lastReceived.other;
        if (w.comboBroken || attacker != w.comboAttacker)
        {
            w.comboHits = 0;
            w.comboStart = w.prevDamage;
            w.comboAttacker = static_cast<s8>(attacker);
        }
        w.comboBroken = false;
        w.comboHits = static_cast<u8>(w.comboHits + t.cur.hits > 255 ? 255 : w.comboHits + t.cur.hits);
        const Tracked* a = tracked(attacker);
        if (w.comboHits >= 2 && a != NULL)
        {
            int dealt = static_cast<int>(w.damage) - static_cast<int>(w.comboStart);
            emit(kTechCombo, *a, attacker, kNoValue, 0, w.comboHits, dealt, 0.0f);
        }
    }

    static void sdi(Watch& w, const Tracked& t, int i)
    {
        if (t.cur.hitReceived && !t.cur.shieldReceived)
        {
            w.sdiOn = true;
            w.sdiInputs = w.sdiShifts = 0;
            w.sdiRegion = static_cast<s8>(stickRegion(w.body.stickX, w.body.stickY));
            return;
        }
        if (!w.sdiOn)
        {
            return;
        }
        if (t.cur.hitlag > 0)
        {
            int region = stickRegion(w.body.stickX, w.body.stickY);
            if (region >= 0 && region != w.sdiRegion && w.sdiInputs < 255)
            {
                w.sdiInputs++;
            }
            w.sdiRegion = static_cast<s8>(region);
            float dx = w.body.x - w.prevBody.x;
            float dy = w.body.y - w.prevBody.y;
            if ((dx > 0.01f || dx < -0.01f || dy > 0.01f || dy < -0.01f) && w.sdiShifts < 255)
            {
                w.sdiShifts++;
            }
            return;
        }
        w.sdiOn = false;
        if (w.sdiInputs > 0 || w.sdiShifts > 0)
        {
            emit(kTechSdi, t, i, kNoValue, 0, w.sdiShifts, w.sdiInputs, 0.0f);
        }
    }

    static void addCell(Watch& w, u8 cell)
    {
        if (w.cellCount == kLedgeCells)
        {
            for (int k = 1; k < kLedgeCells; k++)
            {
                w.cells[k - 1] = w.cells[k];
            }
            w.cellCount--;
        }
        w.cells[w.cellCount++] = cell;
    }

    // The frame's cell: where it ended, except that a jump or an airdodge
    // begun during the frame shows as that even if the fighter was already
    // past it when the frame ended (a frame-perfect airdodge lands at once).
    static u8 frameCell(const Tracked& t)
    {
        u8 cell = cellOf(t.cur.status);
        for (int k = 0; k < t.eventCount; k++)
        {
            u8 c = cellOf(t.events[k].status);
            if ((c == kCellJump || c == kCellAirdodge) && cell != kCellJump && cell != kCellAirdodge)
            {
                cell = c;
            }
        }
        return cell;
    }

    static u8 readLedgeGrabs(Fighter* fighter)
    {
        const soModuleEnumeration* m = fighterModules(fighter);
        if (m == NULL)
        {
            return 0;
        }
        s32 g = instanceInt(m, kLedgeGrabsInt);
        if (g <= 0)
        {
            return 0;
        }
        return static_cast<u8>(g > 255 ? 255 : g);
    }

    static void ledge(Watch& w, const Tracked& t, int i, s32 f)
    {
        int s = t.cur.status;
        if (w.body.xluFrames > 0)
        {
            w.lastIntangible = f;
        }
        bool grabbed = s == kStatusCliffCatchStart || s == kStatusCliffCatch;
        for (int k = 0; k < t.eventCount; k++)
        {
            int es = t.events[k].status;
            grabbed = grabbed || es == kStatusCliffCatchStart || es == kStatusCliffCatch || es == kStatusCliffWait;
        }
        if (grabbed && w.ledgePhase != 1)
        {
            w.ledgePhase = 1;
            w.cellCount = 0;
            w.waitFrames = 0;
            w.ledgeGrabs = 0;
            w.ledgeAir = false;
            w.adHas = 0;
            w.hangXlu = true;
        }
        if (w.ledgePhase == 0)
        {
            return;
        }
        if (outOfPlay(s) || t.cur.hitstun > 0
            || (w.ledgePhase == 2 && f - w.ledgeLeft > kLedgeGiveUp))
        {
            w.ledgePhase = 0;
            return;
        }
        if (w.ledgePhase == 1)
        {
            if (s == kStatusCliffWait)
            {
                u8 grabs = readLedgeGrabs(t.fighter);
                if (grabs > 0)
                {
                    w.ledgeGrabs = grabs;
                }
                // Past 5 grabs P+ writes 0 on entering CliffWait, wiping
                // what an earlier grab left (ProjectM/Ledge.asm:495-513).
                bool firstWait = w.waitFrames == 0;
                w.hangXlu = (w.waitFrames > 0 && w.hangXlu) || w.body.xluFrames > 0;
                w.waitFrames++;
                addCell(w, kCellLedge);
                if (firstWait && !w.hangXlu && w.ledgeGrabs > kLedgeGrabLimit)
                {
                    emit(kTechCliffwait, t, i, kNoValue, 0, 0, w.ledgeGrabs, 0.0f);
                }
                return;
            }
            if (s == kStatusCliffCatch || s == kStatusCliffCatchStart)
            {
                return;
            }
            if (w.ledgeGrabs == 0)
            {
                w.ledgeGrabs = readLedgeGrabs(t.fighter);
            }
            if (w.waitFrames == 0 && w.ledgeGrabs > kLedgeGrabLimit && w.body.xluFrames == 0)
            {
                w.hangXlu = false;
            }
            w.ledgePhase = 2;
            w.ledgeLeft = f;
            bool option = s > kStatusCliffWait && s <= kStatusCliffJumpEnd;
            int earliest = option ? kEarliestOption : kEarliestDrop;
            // The CliffWait frame it left on (1 = the first).
            int left = w.waitFrames + 1;
            if (left <= kCliffwaitShown)
            {
                emit(kTechCliffwait, t, i, left, left <= earliest, earliest, w.ledgeGrabs, 0.0f);
            }
        }
        if (t.cur.airborne)
        {
            w.ledgeAir = true;
        }
        for (int k = 0; k < t.eventCount; k++)
        {
            if (t.events[k].status == kStatusEscapeAir)
            {
                w.adHas = 1;
                w.adAngle = stickAngle(w.body.stickX, w.body.stickY);
            }
        }
        if (!t.cur.airborne && t.cur.actionable && w.ledgeAir)
        {
            // Positive = intangible frames remaining on first actionable frame;
            // negative = vulnerable frames elapsed since intangibility ended
            // (counting the current frame, so the first vulnerable frame is -1).
            int galint = w.body.xluFrames > 0 ? w.body.xluFrames
                         : w.lastIntangible != kNoFrame ? -(f - w.lastIntangible)
                                                         : 0;
            s_ledgeEvent.done = f;
            s_ledgeEvent.player = t.playerNo;
            s_ledgeEvent.count = static_cast<u8>(w.cellCount);
            memcpy(s_ledgeEvent.cells, w.cells, w.cellCount);
            s_ledgeEvent.galint = static_cast<s16>(galint);
            s_ledgeEvent.ledgeFrames = w.waitFrames;
            s_ledgeEvent.hasAngle = w.adHas;
            s_ledgeEvent.angle = w.adAngle;
            s_ledgeEvent.noXlu = !w.hangXlu;
            s_ledgeEvent.grabCount = w.ledgeGrabs;
            if (!t.cpu)
            {
                s_ledge = s_ledgeEvent;
            }
            emit(kTechGalint, t, i, galint, !w.hangXlu, w.waitFrames, w.ledgeGrabs, w.adAngle);
            w.ledgePhase = 0;
            return;
        }
        addCell(w, frameCell(t));
    }

    static float entryDamage(int entryId, const FighterRef* refs, int n)
    {
        for (int k = 0; k < n; k++)
        {
            if (refs[k].entryId == entryId)
            {
                return damage(refs[k]);
            }
        }
        return 0.0f;
    }

    void techniquesFrame()
    {
        if (s_net == NULL)
        {
            return;
        }
        s32 f = trackerFrame();
        FighterRef refs[kMaxFighters];
        int n = listFighters(refs, kMaxFighters);
        for (int i = 0; i < kMaxTracked; i++)
        {
            const Tracked* t = tracked(i);
            Watch& w = s_net->watch[i];
            TechniqueState& st = s_net->state[i];
            if (t == NULL || t->cur.frame != f)
            {
                if (w.valid)
                {
                    clearWatch(w);
                }
                st.valid = false;
                continue;
            }
            Body body;
            if (!readBody(t->fighter, body))
            {
                st.valid = false;
                continue;
            }
            bool first = !w.valid;
            // Measured from the last frame read here.
            body.vy = first ? 0.0f : body.y - w.body.y;
            w.prevBody = first ? body : w.body;
            w.body = body;
            w.prevDamage = first ? entryDamage(t->entryId, refs, n) : w.damage;
            w.damage = entryDamage(t->entryId, refs, n);
            w.valid = true;
            // Technique OSDs skip the Ice Climbers' partner so Nana does not
            // double-post onto Popo's stack or overwrite his counters.
            if (!t->partner)
            {
                jumpCancel(w, *t, i, f);
                wavedash(w, *t, i, f);
                lCancel(w, *t, i);
                actOutOf(w, *t, i, f);
                fastfall(w, *t, i, f);
                airborne(w, *t, i, f);
                ground(w, *t, i, f);
                combo(w, *t, i);
                sdi(w, *t, i);
                ledge(w, *t, i, f);
            }
            st.valid = true;
            st.body = body;
            st.missedLCancel = w.lcMissed;
        }
    }
}
}
