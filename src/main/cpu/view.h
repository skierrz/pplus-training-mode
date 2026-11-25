#pragma once

#include <types.h>

class Fighter;

// What the CPU logic reads (and the little it writes) on a fighter, at the
// moment its controls are read: the values the previous frame left.
namespace tm {
namespace cpu {
    struct View {
        int status;     // status kind
        int hitstun;       // hitstun frames left
        int hitlag;        // hitlag frames left
        bool damageHitlag; // hitlag is from taking damage (soStopModule::isDamage)
        int shieldStun;    // shield stun frames left (GuardDamage only)
        int guardOff;   // GuardOff: frames left before it allows interrupts (RA-Basic[5])
        bool air;       // situation is Air
        u32 groups;     // bit g: interrupt group g enabled
        float x, y;     // position
        float vx, vy;   // position change over the last frame
        float lr;       // facing: +1 right, -1 left
        // Animation frame, speed and last frame. The jump squat's speed
        // ends the animation with it (observed: Fox, 3 frames at 0.67).
        float motionFrame, motionRate, motionEnd;
        // Landing: the landing frame parameter; -1 in other statuses.
        float interruptAt;
        // A dodge whose script opens interrupts in the coming frame (the
        // extra module's frame data service; false without it).
        bool opensNext;
    };

    // Whether the match setup has a human in player slot `playerNo` (the
    // AI flag says "human" for every CPU we drive, so it cannot tell).
    bool humanPlayer(int playerNo);
    // The setup's controller port (1-4, 0 none), which Training copies
    // into the input's controller each frame.
    int controllerOf(int playerNo);
    void setControllerOf(int playerNo, int port);

    // The entry's active fighter, or its partner (Nana) with `sub`.
    Fighter* fighterOf(int entryId, bool sub);
    bool readView(Fighter* fighter, View& out);
    // The entry's active fighter kind (ftKind), -1 if unreadable.
    int fighterKind(int entryId);

    // No hitlag or stun, and a free status or an action interrupt group
    // of its situation enabled.
#if defined(TM_MODULE_EXTRA) && defined(__MWERKS__)
    bool x_canAct(const View& v);
#ifndef TM_EXTRA_THUNK_IMPL
    inline bool canAct(const View& v) { return x_canAct(v); }
#else
    bool canAct(const View& v);
#endif
#else
    bool canAct(const View& v);
#endif

    // From the extra module's frame data service: 0 if it can act now, k
    // if its interrupts open during the k-th coming frame, -1 if unknown or
    // without the extra module.
    int framesUntilActionable(Fighter* fighter);

    // True if the coming frame is the first airborne one; true if it is
    // the animation's last.
    bool takeoffNext(const View& v);
    bool animationEndsNext(const View& v);
    // True if the fighter still has an aerial jump (LA-Basic[1] < Jump_Count_Max).
    bool jumpLeft(Fighter* fighter);
    // True if the fighter's controller already has a live guard tech window
    // (m_triggerData[4]: prev >= 40 && count < 20).
    bool techWindowActive(Fighter* fighter);
    // Spoofs guard trigger indices 3 and 4 on soControllerImpl while a CPU is
    // recovering with a non-None tech option and Tech Trap off.
    void spoofWallTechTrigger(Fighter* fighter);

    // Instance work float 3, Float_Guard_Shield.
    bool shieldHealth(Fighter* fighter, float& out);
    void setShieldHealth(Fighter* fighter, float value);

    void setIntangible(Fighter* fighter, int frames);
    // Brawl's armour for every hit but an absolute one, and the hurtboxes
    // out of grab boxes' reach.
    void setNoReaction(Fighter* fighter, bool on);

    // The kinetic module's damage energy: set when the hit lands, applied
    // when hitlag ends. clearSpeedAll leaves it, and it does not decay
    // while the fighter stands.
    bool knockback(Fighter* fighter, float& x, float& y);
    void clearKnockback(Fighter* fighter);
    // Gravity energy (kinetic energy id 1, as display/di_display.cpp
    // reads it): fall acceleration (negative), largest fall speed, vertical
    // speed; false if the energy is not where it was measured.
    bool fallSpeeds(Fighter* fighter, float& accel, float& maxFall, float& speedY);

    // LA-Float[index].
    bool setWorkFloat(Fighter* fighter, int index, float value);

    // Takes L (or R) out of P+'s trigger variables: see view.cpp.
    void hideAnalogTrigger(Fighter* fighter, bool right);

    void setMotionRate(Fighter* fighter, float rate);
    // Stops every kinetic energy first.
    void placeAt(Fighter* fighter, float x, float y);
    void requestStatus(Fighter* fighter, int status);

    // soSoundModule::playSE; its handle, negative if none.
    int playSound(Fighter* fighter, int soundId);

    // Active hitboxes' centres and radii, at most `max` (radius 0:
    // inactive); returns the count written.
    int attackParts(Fighter* fighter, float* x, float* y, float* size, int max);

    // The fighter's shield bubble when guarding, read from its shield module
    // (or the fixed fallback when unreadable).
    struct ShieldBubble {
        float x, y;
        float radius;
        bool fallback;
    };

    // True if the fighter reflected a projectile on the last frame while
    // guarding (Guard_On / Guard with RA-Bit[10] set).
    bool justReflected(Fighter* fighter);

    // What triggered incomingHit().
    enum IncomingKind { kHitNone = 0, kHitFighter = 1, kHitWeapon = 2, kHitItem = 3 };

    // True if any live hitbox of another fighter (or partner), weapon, or
    // item that can hit `self` sweeps into `self`'s shield bubble over the
    // coming frame. Writes the bubble used to `bubble` and returns the kind
    // of threat (kHitNone if none).
    int incomingHit(Fighter* self, int selfEntry, const View& v, ShieldBubble& bubble);

    // The game's collision check (soGroundModuleImpl::rayCheck -> stRayCheck)
    // from (x0, y0) along the direction vector (dx, dy): true and the height
    // where it meets a ground line. A ray straight down is (x, y, 0, -depth).
    bool groundBetween(Fighter* fighter, float x0, float y0, float dx, float dy, float& hitY);
    // True if a ray from (x, y) along vector (dx, dy) meets a solid wall or
    // ceiling (normal.y < 0.5, skipping passable platforms via stRayCheck).
    bool wallOrCeilBetween(float x, float y, float dx, float dy);
}
}
