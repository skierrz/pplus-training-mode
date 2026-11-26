#include "main/cpu/actions.h"

#include "main/cpu/fighter_input.h"
#ifdef __MWERKS__
#include "shared/game/heap.h"
#endif

// Button bits as the input object holds them (fighter_input.h). The smash
// attacks and aerials are written the way a player's C-stick arrives:
// attack + smash + C-stick with the main stick pushed for one frame (then
// let go, so no C-stick-held frames follow). Tilts are A with the stick
// part way, below the smash threshold. Nothing here depends on the
// character: jump squats, dashes and landings are waited for, not counted.
namespace tm {
namespace cpu {
    using namespace button;

    enum {
        A = kAttack,
        B = kSpecial,
        X = kJump,
        G = kGuard,
        C = kAttack | kSmash | kCStick,
        kTilt = 50,          // stick for a tilt attack
        kWavedashX = 80,     // airdodge angle of the wavedashes
        kWavedashY = -60,
        kAngledX = 60,       // angled up specials
        kAngledY = 80,
        kDashSmashFrames = 3
    };

#define TM_STEP(buttons, x, y, ref, frames) {(buttons), (x), (y), (ref), (frames), kUntilNone, 0}
#define TM_UNTIL(buttons, x, y, ref, until, frames) {(buttons), (x), (y), (ref), (frames), (until), 0}
#define TM_END {0, 0, 0, kRefAbs, 0, kUntilNone, 0}
// A jump whose next step lands on the first airborne frame.
#define TM_TAKEOFF TM_STEP(X, 0, 0, kRefAbs, 1), TM_UNTIL(0, 0, 0, kRefAbs, kUntilTakeoff, 30)

    // ---- ground (after grounded hitstun) ---------------------------------
    static const Step kSpotdodge[] = {TM_STEP(G, 0, -100, kRefAbs, 2), TM_END};
    static const Step kShield[] = {TM_STEP(G, 0, 0, kRefAbs, 20), TM_END};
    static const Step kGrab[] = {TM_STEP(A | G, 0, 0, kRefAbs, 1), TM_END};
    static const Step kFTilt[] = {TM_STEP(A, kTilt, 0, kRefToward, 1), TM_END};
    static const Step kUTilt[] = {TM_STEP(A, 0, kTilt, kRefAbs, 1), TM_END};
    static const Step kDTilt[] = {TM_STEP(A, 0, -kTilt, kRefAbs, 1), TM_END};
    static const Step kRollAway[] = {TM_STEP(G, 100, 0, kRefAway, 2), TM_END};
    static const Step kRollToward[] = {TM_STEP(G, 100, 0, kRefToward, 2), TM_END};
    static const Step kRollRandom[] = {TM_STEP(G, 100, 0, kRefRandom, 2), TM_END};
    static const Step kShortHop[] = {TM_STEP(X, 0, 0, kRefAbs, 1), TM_END};
    // Jump held through the whole jump squat, however long it is.
    static const Step kFullHop[] = {TM_UNTIL(X, 0, 0, kRefAbs, kUntilAir, 30), TM_END};
    // A in the first frames of a dash is read as a smash (the stick has
    // just crossed the smash threshold): the attack waits that out.
    static const Step kDashAttack[] = {TM_UNTIL(0, 100, 0, kRefToward, kUntilDash, 10),
                                       TM_STEP(0, 100, 0, kRefToward, kDashSmashFrames),
                                       TM_STEP(A, 100, 0, kRefToward, 1), TM_END};
    static const Step kDashBack[] = {TM_UNTIL(0, 100, 0, kRefAway, kUntilDash, 10), TM_STEP(0, 100, 0, kRefAway, 1),
                                     TM_END};
    static const Step kDashThrough[] = {TM_UNTIL(0, 100, 0, kRefToward, kUntilDash, 10),
                                        TM_STEP(0, 100, 0, kRefToward, 1), TM_END};
    // Airdodge on the first airborne frame.
    static const Step kWavedashAway[] = {TM_TAKEOFF, TM_STEP(G, kWavedashX, kWavedashY, kRefAway, 1), TM_END};
    static const Step kWavedashToward[] = {TM_TAKEOFF, TM_STEP(G, kWavedashX, kWavedashY, kRefToward, 1), TM_END};
    static const Step kWavedashDown[] = {TM_TAKEOFF, TM_STEP(G, 0, -100, kRefAbs, 1), TM_END};

    // ---- air (after aerial hitstun) ---------------------------------------
    // Guard with the stick sideways (in tumble a neutral guard was not seen
    // to air dodge, a sideways one was; observed, character not recorded).
    static const Step kAirdodge[] = {TM_STEP(G, 100, 0, kRefToward, 1), TM_END};
    // A sideways push in tumble leaves it (a wiggle) instead of jumping, also
    // on the frame after a jump pressed a frame early (it comes out then):
    // the stick goes sideways, as drift, once the jump has started; the
    // angled up specials likewise start straight up.
    static const Step kJumpAway[] = {TM_STEP(X, 0, 0, kRefAbs, 1), TM_UNTIL(0, 0, 0, kRefAbs, kUntilJumped, 2),
                                     TM_STEP(0, 100, 0, kRefAway, 6), TM_END};
    static const Step kJumpToward[] = {TM_STEP(X, 0, 0, kRefAbs, 1), TM_UNTIL(0, 0, 0, kRefAbs, kUntilJumped, 2),
                                       TM_STEP(0, 100, 0, kRefToward, 6), TM_END};
    static const Step kUpBAway[] = {TM_STEP(B, 0, 100, kRefAbs, 1), TM_UNTIL(0, 0, 100, kRefAbs, kUntilSpecial, 2),
                                    TM_STEP(0, kAngledX, kAngledY, kRefAway, 5), TM_END};
    static const Step kUpBToward[] = {TM_STEP(B, 0, 100, kRefAbs, 1), TM_UNTIL(0, 0, 100, kRefAbs, kUntilSpecial, 2),
                                      TM_STEP(0, kAngledX, kAngledY, kRefToward, 5), TM_END};
    // Fastfalls: a down flick once the fall begins (waited for up to 4
    // seconds: light fighters rise long), in tumble, or after a sideways
    // push left tumble (a wiggle).
// Down flicked every other frame until the fall speed jumps (some
// characters can only fastfall some frames after the fall begins).
#define TM_FLICK TM_UNTIL(0, 0, -100, kRefAbs, kUntilFastfall, 1), TM_UNTIL(0, 0, 0, kRefAbs, kUntilFastfall, 1)
#define TM_FLICKS TM_FLICK, TM_FLICK, TM_FLICK, TM_FLICK, TM_FLICK, TM_FLICK, TM_FLICK, TM_FLICK
    static const Step kWiggleFastfall[] = {TM_STEP(0, 100, 0, kRefToward, 1), TM_STEP(0, 0, 0, kRefAbs, 1),
                                           TM_UNTIL(0, 0, 0, kRefAbs, kUntilFalling, 240), TM_FLICKS, TM_END};
    static const Step* const kTumbleFastfall = kWiggleFastfall + 2;
    static const Step kWait[] = {TM_STEP(0, 0, 0, kRefAbs, 1), TM_END};
    // One-frame sideways wiggle out of tumble (MoveDef/Misc.asm:648-675).
    static const Step kWiggle[] = {TM_STEP(0, 100, 0, kRefToward, 1), TM_END};

    // ---- out of shield (after shield stun; guard is held until then) ------
    // Jump, then the move on the jump squat's second frame (P+ lets an up
    // smash and an up special cancel the jump squat; not found in
    // Project+'s source, the fighters' JumpSquat scripts not checked).
    static const Step kOosUSmash[] = {TM_STEP(X, 0, 0, kRefAbs, 1), TM_STEP(C, 0, 100, kRefAbs, 1), TM_END};
    static const Step kOosUpB[] = {TM_STEP(X, 0, 0, kRefAbs, 1), TM_STEP(B, 0, 100, kRefAbs, 1),
                                   TM_STEP(0, 0, 100, kRefAbs, 5), TM_END};
    // The other specials from the first airborne frame of a jump.
    static const Step kOosSideBAway[] = {TM_TAKEOFF, TM_STEP(B, 100, 0, kRefAway, 1), TM_END};
    static const Step kOosSideBToward[] = {TM_TAKEOFF, TM_STEP(B, 100, 0, kRefToward, 1), TM_END};
    static const Step kOosDownB[] = {TM_TAKEOFF, TM_STEP(B, 0, -100, kRefAbs, 1), TM_END};
    static const Step kOosNeutralB[] = {TM_TAKEOFF, TM_STEP(B, 0, 0, kRefAbs, 1), TM_END};
    // A smash attack needs the shield dropped first: guard let go, the
    // smash on the first frame the fighter can act.
    static const Step kOosDSmash[] = {TM_STEP(0, 0, 0, kRefAbs, 1), TM_UNTIL(0, 0, 0, kRefAbs, kUntilCanAct, 30),
                                      TM_STEP(C, 0, -100, kRefAbs, 1), TM_END};
    static const Step kOosFSmash[] = {TM_STEP(0, 0, 0, kRefAbs, 1), TM_UNTIL(0, 0, 0, kRefAbs, kUntilCanAct, 30),
                                      TM_STEP(C, 100, 0, kRefToward, 1), TM_END};
    // Grounded suffixes shared directly with their OoS counterparts.
    static const Step* const kUpB = kOosUpB + 1;
    static const Step* const kUSmash = kOosUSmash + 1;
    static const Step* const kDSmash = kOosDSmash + 2;
    static const Step* const kFSmash = kOosFSmash + 2;
    static const Step* const kSideBAway = kOosSideBAway + 2;
    static const Step* const kSideBToward = kOosSideBToward + 2;
    static const Step* const kDownB = kOosDownB + 2;
    static const Step* const kNeutralB = kOosNeutralB + 2;

#ifdef __MWERKS__
    // 5 aerials (Nair, Fair, Bair, Uair, Dair) expanded into SH, DJC, Float,
    // Ganon Float, SHFFL, and FFL scripts in Heaps::Network at attach time.
    static const Step kAerials[5] = {
        TM_STEP(A, 0, 0, kRefAbs, 1),
        TM_STEP(C, 100, 0, kRefForward, 1),
        TM_STEP(C, -100, 0, kRefForward, 1),
        TM_STEP(C, 0, 100, kRefAbs, 1),
        TM_STEP(C, 0, -100, kRefAbs, 1)
    };
#else
    static const Step kJab[] = {TM_STEP(A, 0, 0, kRefAbs, 1), TM_END};
    static const Step kFair[] = {TM_STEP(C, 100, 0, kRefForward, 1), TM_END};
    static const Step kBair[] = {TM_STEP(C, -100, 0, kRefForward, 1), TM_END};
    // Short hop aerials, the aerial on the first airborne frame.
    static const Step kSHNair[] = {TM_TAKEOFF, TM_STEP(A, 0, 0, kRefAbs, 1), TM_END};
    static const Step kSHFair[] = {TM_TAKEOFF, TM_STEP(C, 100, 0, kRefForward, 1), TM_END};
    static const Step kSHBair[] = {TM_TAKEOFF, TM_STEP(C, -100, 0, kRefForward, 1), TM_END};
    static const Step kSHUair[] = {TM_TAKEOFF, TM_STEP(C, 0, 100, kRefAbs, 1), TM_END};
    static const Step kSHDair[] = {TM_TAKEOFF, TM_STEP(C, 0, -100, kRefAbs, 1), TM_END};
    // Canonical Frame-Perfect SHFFL and FFL templates; options.cpp expands them
    // with CpuOptions::shfflTiming via shffl_core.h, so 1 flick pair suffices.
#define TM_SHFFL_SCRIPT(aerial) \
    {TM_TAKEOFF, aerial, TM_UNTIL(0, 0, 0, kRefAbs, kUntilFalling, 60), TM_FLICK, TM_END}
#define TM_FFL_SCRIPT(aerial) \
    {aerial, TM_UNTIL(0, 0, 0, kRefAbs, kUntilFalling, 240), TM_FLICK, TM_END}
    static const Step kShfflNair[] = TM_SHFFL_SCRIPT(TM_STEP(A, 0, 0, kRefAbs, 1));
    static const Step kShfflFair[] = TM_SHFFL_SCRIPT(TM_STEP(C, 100, 0, kRefForward, 1));
    static const Step kShfflBair[] = TM_SHFFL_SCRIPT(TM_STEP(C, -100, 0, kRefForward, 1));
    static const Step kShfflUair[] = TM_SHFFL_SCRIPT(TM_STEP(C, 0, 100, kRefAbs, 1));
    static const Step kShfflDair[] = TM_SHFFL_SCRIPT(TM_STEP(C, 0, -100, kRefAbs, 1));
    static const Step kFflNair[] = TM_FFL_SCRIPT(TM_STEP(A, 0, 0, kRefAbs, 1));
    static const Step kFflFair[] = TM_FFL_SCRIPT(TM_STEP(C, 100, 0, kRefForward, 1));
    static const Step kFflBair[] = TM_FFL_SCRIPT(TM_STEP(C, -100, 0, kRefForward, 1));
    static const Step kFflUair[] = TM_FFL_SCRIPT(TM_STEP(C, 0, 100, kRefAbs, 1));
    static const Step kFflDair[] = TM_FFL_SCRIPT(TM_STEP(C, 0, -100, kRefAbs, 1));
#undef TM_SHFFL_SCRIPT
#undef TM_FFL_SCRIPT
    // ---- P+ character techniques (ground and shield lists) ---------------
#define TM_DJC(aerial) {TM_TAKEOFF, TM_STEP(X, 0, 0, kRefAbs, 1), aerial, TM_END}
    static const Step kDjcNair[] = TM_DJC(TM_STEP(A, 0, 0, kRefAbs, 1));
    static const Step kDjcFair[] = TM_DJC(TM_STEP(C, 100, 0, kRefForward, 1));
    static const Step kDjcBair[] = TM_DJC(TM_STEP(C, -100, 0, kRefForward, 1));
    static const Step kDjcUair[] = TM_DJC(TM_STEP(C, 0, 100, kRefAbs, 1));
    static const Step kDjcDair[] = TM_DJC(TM_STEP(C, 0, -100, kRefAbs, 1));
#undef TM_DJC
#define TM_FLOAT(aerial) \
    {TM_STEP(X, 0, 0, kRefAbs, 1), TM_UNTIL(X, 0, -100, kRefAbs, kUntilSpecial, 30), aerial, TM_END}
    static const Step kFloatNair[] = TM_FLOAT(TM_STEP(X | A, 0, 0, kRefAbs, 1));
    static const Step kFloatFair[] = TM_FLOAT(TM_STEP(X | C, 100, 0, kRefForward, 1));
    static const Step kFloatBair[] = TM_FLOAT(TM_STEP(X | C, -100, 0, kRefForward, 1));
    static const Step kFloatUair[] = TM_FLOAT(TM_STEP(X | C, 0, 100, kRefAbs, 1));
    static const Step kFloatDair[] = TM_FLOAT(TM_STEP(X | C, 0, -100, kRefAbs, 1));
#undef TM_FLOAT
#define TM_GANON_FLOAT(aerial) \
    {TM_TAKEOFF, TM_STEP(B, 0, 0, kRefAbs, 1), TM_STEP(0, 0, 0, kRefAbs, 5), aerial, TM_END}
    static const Step kGanonFloatNair[] = TM_GANON_FLOAT(TM_STEP(A, 0, 0, kRefAbs, 1));
    static const Step kGanonFloatFair[] = TM_GANON_FLOAT(TM_STEP(C, 100, 0, kRefForward, 1));
    static const Step kGanonFloatBair[] = TM_GANON_FLOAT(TM_STEP(C, -100, 0, kRefForward, 1));
    static const Step kGanonFloatUair[] = TM_GANON_FLOAT(TM_STEP(C, 0, 100, kRefAbs, 1));
    static const Step kGanonFloatDair[] = TM_GANON_FLOAT(TM_STEP(C, 0, -100, kRefAbs, 1));
#undef TM_GANON_FLOAT
#endif
#undef TM_FLICK
#undef TM_FLICKS
#undef TM_STEP
#undef TM_UNTIL
#undef TM_END
#undef TM_TAKEOFF

#define TM_NONE {NULL, 0, 0}
#define TM_RANDOM {NULL, kActionRandom, 0}
#define TM_RANDOM_ADV {NULL, kActionRandomAdvanced, 0}
#define TM_CUSTOM_POOL {NULL, kActionCustomPool, 0}
#define TM_DO(steps) {steps, 0, 0}

    // Deduplicated unique Action records; each list below stores 1-byte indices
    // into kActions rather than duplicating 8-byte Action structs + relocs.
    enum ActId {
        A_NONE = 0,
        A_SPOTDODGE, A_SHIELD, A_GRAB, A_UPB, A_SIDEB_T, A_DOWNB, A_NEUTRALB,
        A_USMASH, A_DSMASH, A_FSMASH, A_ROLL_A, A_ROLL_T, A_JAB, A_FTILT, A_UTILT, A_DTILT,
        A_DASH_ATK, A_SH, A_FH, A_WD_A, A_WD_T, A_WD_D, A_DASH_B, A_DASH_THRU,
        A_SLOT1, A_SLOT2, A_SLOT3, A_SLOT4, A_SLOT5, A_SLOT6, A_PLAY_RND,
        A_SIDEB_A, A_ROLL_RND,
        A_SH_NAIR, A_SH_FAIR, A_SH_BAIR, A_SH_UAIR, A_SH_DAIR,
        A_RND_ADV, A_RND,
        A_DJC_NAIR, A_DJC_FAIR, A_DJC_BAIR, A_DJC_UAIR, A_DJC_DAIR,
        A_FLT_NAIR, A_FLT_FAIR, A_FLT_BAIR, A_FLT_UAIR, A_FLT_DAIR,
        A_SHFFL_NAIR, A_SHFFL_FAIR, A_SHFFL_BAIR, A_SHFFL_UAIR, A_SHFFL_DAIR,
        A_CUSTOM_POOL,
        A_AIRDODGE, A_JUMP_A, A_JUMP_T, A_JUMP_N, A_UPB_A, A_UPB_T, A_FAIR, A_BAIR,
        A_TUMBLE_FF, A_WIGGLE_FF, A_WAIT_NOTECH,
        A_FFL_NAIR, A_FFL_FAIR, A_FFL_BAIR, A_FFL_UAIR, A_FFL_DAIR,
        A_OOS_UPB, A_OOS_SIDEB_A, A_OOS_SIDEB_T, A_OOS_DOWNB, A_OOS_NEUTRALB,
        A_OOS_USMASH, A_OOS_DSMASH, A_OOS_FSMASH, A_WAIT, A_COUNT
    };

#ifdef __MWERKS__
    struct ActionsNet {
        Action actions[A_COUNT];
        Action ganonFloats[5];
        Step sh[5][4];
        Step djc[5][5];
        Step flt[5][4];
        Step gflt[5][6];
        Step shffl[5][7];
        Step ffl[5][5];
    };
    static ActionsNet* s_net = NULL;

    struct StaticActionSpec {
        u8 id;
        u8 flags;
        const Step* steps;
    };

    static const StaticActionSpec kStaticActs[] = {
        {A_SPOTDODGE, 0, kSpotdodge}, {A_SHIELD, 0, kShield}, {A_GRAB, 0, kGrab}, {A_UPB, 0, kUpB},
        {A_SIDEB_T, 0, kSideBToward}, {A_DOWNB, 0, kDownB}, {A_NEUTRALB, 0, kNeutralB},
        {A_USMASH, 0, kUSmash}, {A_DSMASH, 0, kDSmash}, {A_FSMASH, 0, kFSmash},
        {A_ROLL_A, 0, kRollAway}, {A_ROLL_T, 0, kRollToward}, {A_FTILT, 0, kFTilt},
        {A_UTILT, 0, kUTilt}, {A_DTILT, 0, kDTilt}, {A_DASH_ATK, 0, kDashAttack},
        {A_SH, 0, kShortHop}, {A_FH, 0, kFullHop}, {A_WD_A, 0, kWavedashAway},
        {A_WD_T, 0, kWavedashToward}, {A_WD_D, 0, kWavedashDown}, {A_DASH_B, 0, kDashBack},
        {A_DASH_THRU, kEndsWithSteps, kDashThrough}, {A_SIDEB_A, 0, kSideBAway},
        {A_ROLL_RND, 0, kRollRandom}, {A_AIRDODGE, 0, kAirdodge}, {A_JUMP_A, kNeedsJump, kJumpAway},
        {A_JUMP_T, kNeedsJump, kJumpToward}, {A_JUMP_N, kNeedsJump, kShortHop},
        {A_UPB_A, 0, kUpBAway}, {A_UPB_T, 0, kUpBToward},
        {A_TUMBLE_FF, kEndsWithSteps, kTumbleFastfall}, {A_WIGGLE_FF, kEndsWithSteps, kWiggleFastfall},
        {A_WAIT_NOTECH, kNoTech | kEndsWithSteps, kWait},
        {A_OOS_UPB, 0, kOosUpB}, {A_OOS_SIDEB_A, 0, kOosSideBAway}, {A_OOS_SIDEB_T, 0, kOosSideBToward},
        {A_OOS_DOWNB, 0, kOosDownB}, {A_OOS_NEUTRALB, 0, kOosNeutralB},
        {A_OOS_USMASH, 0, kOosUSmash}, {A_OOS_DSMASH, 0, kOosDSmash}, {A_OOS_FSMASH, 0, kOosFSmash},
        {A_WAIT, 0, kWait}
    };

    static void setAct(Action& a, const Step* steps, u16 flags)
    {
        a.steps = steps;
        a.flags = flags;
    }

    void actionsAttach()
    {
        if (s_net == NULL)
        {
            s_net = static_cast<ActionsNet*>(game::allocNetworkZero(static_cast<u32>(sizeof(ActionsNet))));
        }
        if (s_net == NULL)
        {
            return;
        }
        ActionsNet& n = *s_net;
        for (u32 i = 0; i < sizeof(kStaticActs) / sizeof(kStaticActs[0]); i++)
        {
            setAct(n.actions[kStaticActs[i].id], kStaticActs[i].steps, kStaticActs[i].flags);
        }
        for (u16 i = 0; i < 6; i++)
        {
            n.actions[A_SLOT1 + i].playSlot = static_cast<u16>(i + 1);
        }
        n.actions[A_PLAY_RND].flags = kActionPlayRandomSlot;
        n.actions[A_RND_ADV].flags = kActionRandomAdvanced;
        n.actions[A_RND].flags = kActionRandom;
        n.actions[A_CUSTOM_POOL].flags = kActionCustomPool;

        static const Step kJump1 = {X, 0, 0, kRefAbs, 1, kUntilNone, 0};
        static const Step kTakeoff1 = {0, 0, 0, kRefAbs, 30, kUntilTakeoff, 0};
        static const Step kFloatWait = {X, 0, -100, kRefAbs, 30, kUntilSpecial, 0};
        static const Step kGanonB = {B, 0, 0, kRefAbs, 1, kUntilNone, 0};
        static const Step kWait5 = {0, 0, 0, kRefAbs, 5, kUntilNone, 0};
        static const Step kFall60 = {0, 0, 0, kRefAbs, 60, kUntilFalling, 0};
        static const Step kFall240 = {0, 0, 0, kRefAbs, 240, kUntilFalling, 0};
        static const Step kFlick0 = {0, 0, -100, kRefAbs, 1, kUntilFastfall, 0};
        static const Step kFlick1 = {0, 0, 0, kRefAbs, 1, kUntilFastfall, 0};

        for (int i = 0; i < 5; i++)
        {
            const Step& ae = kAerials[i];
            n.sh[i][0] = kJump1; n.sh[i][1] = kTakeoff1; n.sh[i][2] = ae;
            n.djc[i][0] = kJump1; n.djc[i][1] = kTakeoff1; n.djc[i][2] = kJump1; n.djc[i][3] = ae;
            n.flt[i][0] = kJump1; n.flt[i][1] = kFloatWait; n.flt[i][2] = ae;
            n.flt[i][2].buttons = static_cast<u16>(n.flt[i][2].buttons | X);
            n.gflt[i][0] = kJump1; n.gflt[i][1] = kTakeoff1; n.gflt[i][2] = kGanonB;
            n.gflt[i][3] = kWait5; n.gflt[i][4] = ae;
            n.shffl[i][0] = kJump1; n.shffl[i][1] = kTakeoff1; n.shffl[i][2] = ae;
            n.shffl[i][3] = kFall60; n.shffl[i][4] = kFlick0; n.shffl[i][5] = kFlick1;
            n.ffl[i][0] = ae; n.ffl[i][1] = kFall240; n.ffl[i][2] = kFlick0; n.ffl[i][3] = kFlick1;

            setAct(n.actions[A_SH_NAIR + i], n.sh[i], 0);
            setAct(n.actions[A_DJC_NAIR + i], n.djc[i], kForDjc);
            setAct(n.actions[A_FLT_NAIR + i], n.flt[i], kForFloat);
            setAct(n.actions[A_SHFFL_NAIR + i], n.shffl[i], kForShffl | kEndsWithSteps);
            setAct(n.actions[A_FFL_NAIR + i], n.ffl[i], kForFfl | kEndsWithSteps);
            setAct(n.ganonFloats[i], n.gflt[i], kForFloat);
        }
        // Single-aerial / jab scripts share the {ae, TM_END} suffix of n.sh[].
        setAct(n.actions[A_JAB], n.sh[0] + 2, 0);
        setAct(n.actions[A_FAIR], n.sh[1] + 2, 0);
        setAct(n.actions[A_BAIR], n.sh[2] + 2, 0);
    }

    void actionsDetach()
    {
        game::freeNetworkSlot(reinterpret_cast<void**>(&s_net));
    }

    static const Action* actionsTable()
    {
        return s_net != NULL ? s_net->actions : NULL;
    }

    static const Action* ganonFloatsTable()
    {
        return s_net != NULL ? s_net->ganonFloats : NULL;
    }
#else
    static const Action kActions[] = {
        TM_NONE,
        TM_DO(kSpotdodge), TM_DO(kShield), TM_DO(kGrab), TM_DO(kUpB), TM_DO(kSideBToward), TM_DO(kDownB), TM_DO(kNeutralB),
        TM_DO(kUSmash), TM_DO(kDSmash), TM_DO(kFSmash), TM_DO(kRollAway), TM_DO(kRollToward), TM_DO(kJab), TM_DO(kFTilt), TM_DO(kUTilt), TM_DO(kDTilt),
        TM_DO(kDashAttack), TM_DO(kShortHop), TM_DO(kFullHop), TM_DO(kWavedashAway), TM_DO(kWavedashToward), TM_DO(kWavedashDown), TM_DO(kDashBack), {kDashThrough, kEndsWithSteps, 0},
        {NULL, 0, 1}, {NULL, 0, 2}, {NULL, 0, 3}, {NULL, 0, 4}, {NULL, 0, 5}, {NULL, 0, 6}, {NULL, kActionPlayRandomSlot, 0},
        TM_DO(kSideBAway), TM_DO(kRollRandom),
        TM_DO(kSHNair), TM_DO(kSHFair), TM_DO(kSHBair), TM_DO(kSHUair), TM_DO(kSHDair),
        TM_RANDOM_ADV, TM_RANDOM,
        {kDjcNair, kForDjc, 0}, {kDjcFair, kForDjc, 0}, {kDjcBair, kForDjc, 0}, {kDjcUair, kForDjc, 0}, {kDjcDair, kForDjc, 0},
        {kFloatNair, kForFloat, 0}, {kFloatFair, kForFloat, 0}, {kFloatBair, kForFloat, 0}, {kFloatUair, kForFloat, 0}, {kFloatDair, kForFloat, 0},
        {kShfflNair, kForShffl | kEndsWithSteps, 0}, {kShfflFair, kForShffl | kEndsWithSteps, 0}, {kShfflBair, kForShffl | kEndsWithSteps, 0}, {kShfflUair, kForShffl | kEndsWithSteps, 0}, {kShfflDair, kForShffl | kEndsWithSteps, 0},
        TM_CUSTOM_POOL,
        TM_DO(kAirdodge), {kJumpAway, kNeedsJump, 0}, {kJumpToward, kNeedsJump, 0}, {kShortHop, kNeedsJump, 0}, TM_DO(kUpBAway), TM_DO(kUpBToward), TM_DO(kFair), TM_DO(kBair),
        {kTumbleFastfall, kEndsWithSteps, 0}, {kWiggleFastfall, kEndsWithSteps, 0}, {kWait, kNoTech | kEndsWithSteps, 0},
        {kFflNair, kForFfl | kEndsWithSteps, 0}, {kFflFair, kForFfl | kEndsWithSteps, 0}, {kFflBair, kForFfl | kEndsWithSteps, 0}, {kFflUair, kForFfl | kEndsWithSteps, 0}, {kFflDair, kForFfl | kEndsWithSteps, 0},
        TM_DO(kOosUpB), TM_DO(kOosSideBAway), TM_DO(kOosSideBToward), TM_DO(kOosDownB), TM_DO(kOosNeutralB),
        TM_DO(kOosUSmash), TM_DO(kOosDSmash), TM_DO(kOosFSmash), TM_DO(kWait)
    };

    static const Action kGanonFloats[] = {
        {kGanonFloatNair, kForFloat, 0}, {kGanonFloatFair, kForFloat, 0}, {kGanonFloatBair, kForFloat, 0},
        {kGanonFloatUair, kForFloat, 0}, {kGanonFloatDair, kForFloat, 0}};

    static const Action* actionsTable()
    {
        return kActions;
    }

    static const Action* ganonFloatsTable()
    {
        return kGanonFloats;
    }
#endif

#define A_SLOTS A_SLOT1, A_SLOT2, A_SLOT3, A_SLOT4, A_SLOT5, A_SLOT6, A_PLAY_RND
#define A_SHS A_SH_NAIR, A_SH_FAIR, A_SH_BAIR, A_SH_UAIR, A_SH_DAIR
#define A_DJCS A_DJC_NAIR, A_DJC_FAIR, A_DJC_BAIR, A_DJC_UAIR, A_DJC_DAIR
#define A_FLTS A_FLT_NAIR, A_FLT_FAIR, A_FLT_BAIR, A_FLT_UAIR, A_FLT_DAIR
#define A_SHFFLS A_SHFFL_NAIR, A_SHFFL_FAIR, A_SHFFL_BAIR, A_SHFFL_UAIR, A_SHFFL_DAIR
#define A_FFLS A_FFL_NAIR, A_FFL_FAIR, A_FFL_BAIR, A_FFL_UAIR, A_FFL_DAIR

    // Indexed like the Counter Action rows' labels (ui/pages/cpu_options.cpp).
    static const u8 kGround[] = {
        A_NONE, A_SPOTDODGE, A_SHIELD, A_GRAB, A_UPB, A_SIDEB_T, A_DOWNB, A_NEUTRALB,
        A_USMASH, A_DSMASH, A_FSMASH, A_ROLL_A, A_ROLL_T, A_JAB, A_FTILT, A_UTILT, A_DTILT,
        A_DASH_ATK, A_SH, A_FH, A_WD_A, A_WD_T, A_WD_D, A_DASH_B, A_DASH_THRU,
        A_SLOTS, A_SIDEB_A, A_ROLL_RND, A_SHS, A_RND_ADV, A_RND, A_DJCS, A_FLTS, A_SHFFLS, A_CUSTOM_POOL};
    static const u8 kAir[] = {
        A_NONE, A_AIRDODGE, A_JUMP_A, A_JUMP_T, A_JUMP_N, A_UPB_A, A_UPB_T, A_NEUTRALB,
        A_SIDEB_A, A_SIDEB_T, A_DOWNB, A_JAB, A_FAIR, A_BAIR, A_USMASH, A_DSMASH,
        A_TUMBLE_FF, A_WIGGLE_FF, A_NONE, A_WAIT_NOTECH,
        A_SLOTS, A_RND_ADV, A_RND, A_FFLS, A_CUSTOM_POOL};
    static const u8 kShieldList[] = {
        A_NONE, A_GRAB, A_SH, A_FH, A_SPOTDODGE, A_ROLL_A, A_ROLL_T, A_OOS_UPB,
        A_OOS_SIDEB_A, A_OOS_SIDEB_T, A_OOS_DOWNB, A_OOS_NEUTRALB, A_OOS_USMASH, A_OOS_DSMASH, A_OOS_FSMASH,
        A_SHS, A_WD_A, A_WAIT, A_SLOTS, A_WD_T, A_WD_D, A_ROLL_RND, A_RND_ADV, A_RND,
        A_DJCS, A_FLTS, A_SHFFLS, A_CUSTOM_POOL};

#undef TM_NONE
#undef TM_RANDOM
#undef TM_RANDOM_ADV
#undef TM_CUSTOM_POOL
#undef TM_DO
#undef A_SLOTS
#undef A_SHS
#undef A_DJCS
#undef A_FLTS
#undef A_SHFFLS
#undef A_FFLS

    // Clean P+ Random counter action pools, indexed into the tables above:
    //  - Ground: defensive options, quick pokes, and movement mixups
    //  - Air: airdodge, directional jumps, and the five aerials
    //  - Shield: grab, jumps, dodges, quick OoS attacks, and wavedash
    static const u8 kGroundRandom[] = {1, 2, 3, 11, 12, 13, 14, 15, 16, 18, 20, 23};
    static const u8 kAirRandom[] = {1, 2, 3, 11, 12, 13, 14, 15};
    static const u8 kShieldRandom[] = {1, 2, 4, 5, 6, 7, 12, 15, 16, 20};

    static const u8* const kLists[kListCount] = {kGround, kAir, kShieldList};
    static const u8 kCounts[kListCount] = {sizeof(kGround), sizeof(kAir), sizeof(kShieldList)};

    int actionKind(int list, int index)
    {
        const Action* table = actionsTable();
        if (table == NULL || list < 0 || list >= kListCount || index <= 0 || index >= kCounts[list])
        {
            return kActionNone;
        }
        const Action& a = table[kLists[list][index]];
        if (a.steps != NULL)
        {
            return kActionScript;
        }
        if (a.playSlot != 0)
        {
            return kActionPlaySlot;
        }
        return a.flags == kActionRandom || a.flags == kActionRandomAdvanced || a.flags == kActionPlayRandomSlot
                       || a.flags == kActionCustomPool
                   ? static_cast<int>(a.flags)
                   : kActionNone;
    }

    // ft_entry.h's ftKind values. Double jump cancels and floats checked
    // in game (Final Destination).
    enum { kNess = 0xA, kPeach = 0xC, kGanon = 0x14, kLucas = 0x1A, kMewtwo = 0x26 };

    const Action* actionFor(int list, int index, int kind)
    {
        int ak = actionKind(list, index);
        if (ak != kActionScript && ak != kActionPlaySlot)
        {
            return NULL;
        }
        const Action* a = &actionsTable()[kLists[list][index]];
        if (kind == kKindAny)
        {
            return a;
        }
        if (kind == kGanon && (a->flags & kForFloat) != 0)
        {
            int offset = static_cast<int>(kLists[list][index]) - A_FLT_NAIR;
            const Action* gf = ganonFloatsTable();
            return gf != NULL && offset >= 0 && offset < 5 ? &gf[offset] : NULL;
        }
        return fitsKind(a, kind) ? a : NULL;
    }

    int randomAction(int list, u32 roll)
    {
        switch (list)
        {
        case kListGround:
            return kGroundRandom[roll % sizeof(kGroundRandom)];
        case kListAir:
            return kAirRandom[roll % sizeof(kAirRandom)];
        default:
            return kShieldRandom[roll % sizeof(kShieldRandom)];
        }
    }

    int customPoolIndex(int list)
    {
        return list >= 0 && list < kListCount ? kCounts[list] - 1 : 0;
    }

    void poolDefaults(int list, u8 bits[8])
    {
        for (int i = 0; i < 8; i++)
        {
            bits[i] = 0;
        }
        const u8* src = kGroundRandom;
        u32 n = sizeof(kGroundRandom);
        if (list == kListAir)
        {
            src = kAirRandom;
            n = sizeof(kAirRandom);
        }
        else if (list == kListShield)
        {
            src = kShieldRandom;
            n = sizeof(kShieldRandom);
        }
        for (u32 i = 0; i < n; i++)
        {
            u8 idx = src[i];
            if (idx < 64)
            {
                bits[idx >> 3] |= static_cast<u8>(1u << (idx & 7));
            }
        }
    }

    void poolSanitize(int list, u8 bits[8])
    {
        if (list < 0 || list >= kListCount)
        {
            return;
        }
        int count = kCounts[list];
        // Bit 0 is always legal ("Nothing (Wait)" = roll no counter).
        for (int idx = 1; idx < 64; idx++)
        {
            u8 mask = static_cast<u8>(1u << (idx & 7));
            if ((bits[idx >> 3] & mask) == 0)
            {
                continue;
            }
            int ak = idx < count ? actionKind(list, idx) : kActionNone;
            if (ak != kActionScript && ak != kActionPlaySlot)
            {
                bits[idx >> 3] &= static_cast<u8>(~mask);
            }
        }
    }

    int randomPoolAction(int list, const u8 bits[8], int kind, u32 roll)
    {
        if (list < 0 || list >= kListCount)
        {
            return 0;
        }
        u8 picks[64];
        int n = 0;
        int count = kCounts[list];
        if (count > 64)
        {
            count = 64;
        }
        if ((bits[0] & 1u) != 0)
        {
            picks[n++] = 0;
        }
        for (int idx = 1; idx < count; idx++)
        {
            if ((bits[idx >> 3] & (1u << (idx & 7))) == 0)
            {
                continue;
            }
            if (actionFor(list, idx, kind) != NULL)
            {
                picks[n++] = static_cast<u8>(idx);
            }
        }
        return n > 0 ? picks[roll % static_cast<u32>(n)] : 0;
    }

    bool fitsKind(const Action* a, int kind)
    {
        if (a->flags & kForDjc)
        {
            return kind == kNess || kind == kLucas || kind == kMewtwo;
        }
        if (a->flags & kForFloat)
        {
            return kind == kPeach || kind == kGanon || kind == kMewtwo;
        }
        return true;
    }

    const Step* shortHop()
    {
        return kShortHop;
    }

    const Action* wiggleOut()
    {
        static const Action kWiggleOut = {kWiggle, kEndsWithSteps, 0};
        return &kWiggleOut;
    }

    int actionCount(int list)
    {
        return list >= 0 && list < kListCount ? kCounts[list] : 0;
    }
}
}
