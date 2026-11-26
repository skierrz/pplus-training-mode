#include "main/cpu/actions.h"

#include "main/cpu/fighter_input.h"

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
    static const Step kJab[] = {TM_STEP(A, 0, 0, kRefAbs, 1), TM_END};
    static const Step kFTilt[] = {TM_STEP(A, kTilt, 0, kRefToward, 1), TM_END};
    static const Step kUTilt[] = {TM_STEP(A, 0, kTilt, kRefAbs, 1), TM_END};
    static const Step kDTilt[] = {TM_STEP(A, 0, -kTilt, kRefAbs, 1), TM_END};
    static const Step kFSmash[] = {TM_STEP(C, 100, 0, kRefToward, 1), TM_END};
    static const Step kUSmash[] = {TM_STEP(C, 0, 100, kRefAbs, 1), TM_END};
    static const Step kDSmash[] = {TM_STEP(C, 0, -100, kRefAbs, 1), TM_END};
    static const Step kUpB[] = {TM_STEP(B, 0, 100, kRefAbs, 1), TM_STEP(0, 0, 100, kRefAbs, 5), TM_END};
    static const Step kNeutralB[] = {TM_STEP(B, 0, 0, kRefAbs, 1), TM_END};
    static const Step kDownB[] = {TM_STEP(B, 0, -100, kRefAbs, 1), TM_END};
    static const Step kSideBToward[] = {TM_STEP(B, 100, 0, kRefToward, 1), TM_END};
    static const Step kSideBAway[] = {TM_STEP(B, 100, 0, kRefAway, 1), TM_END};
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
    // Short hop aerials, the aerial on the first airborne frame.
    static const Step kSHNair[] = {TM_TAKEOFF, TM_STEP(A, 0, 0, kRefAbs, 1), TM_END};
    static const Step kSHFair[] = {TM_TAKEOFF, TM_STEP(C, 100, 0, kRefForward, 1), TM_END};
    static const Step kSHBair[] = {TM_TAKEOFF, TM_STEP(C, -100, 0, kRefForward, 1), TM_END};
    static const Step kSHUair[] = {TM_TAKEOFF, TM_STEP(C, 0, 100, kRefAbs, 1), TM_END};
    static const Step kSHDair[] = {TM_TAKEOFF, TM_STEP(C, 0, -100, kRefAbs, 1), TM_END};

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
    static const Step kFair[] = {TM_STEP(C, 100, 0, kRefForward, 1), TM_END};
    static const Step kBair[] = {TM_STEP(C, -100, 0, kRefForward, 1), TM_END};
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
#undef TM_FLICK
#undef TM_FLICKS
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

    // ---- P+ character techniques (ground and shield lists) ---------------
    // Double jump cancelled aerials: the double jump on the first airborne
    // frame, the aerial on the next with jump let go. Ness and Lucas cancel
    // only then (their aerials set RA-Bit 33 while jump is not held:
    // FitNess.pac 0x10C20, FitLucas.pac 0x2342C; P+ Physics.asm:137-139);
    // Mewtwo's aerials cancel it either way. Peach and Yoshi cancel too, but
    // their double jumps dip first: from the ground they land two frames into
    // the aerial, before it hits (measured), so they are not offered.
#define TM_DJC(aerial) {TM_TAKEOFF, TM_STEP(X, 0, 0, kRefAbs, 1), aerial, TM_END}
    static const Step kDjcNair[] = TM_DJC(TM_STEP(A, 0, 0, kRefAbs, 1));
    static const Step kDjcFair[] = TM_DJC(TM_STEP(C, 100, 0, kRefForward, 1));
    static const Step kDjcBair[] = TM_DJC(TM_STEP(C, -100, 0, kRefForward, 1));
    static const Step kDjcUair[] = TM_DJC(TM_STEP(C, 0, 100, kRefAbs, 1));
    static const Step kDjcDair[] = TM_DJC(TM_STEP(C, 0, -100, kRefAbs, 1));
#undef TM_DJC
    // Float aerials: jump held from the press, the stick down from the jump
    // squat on, floats Peach and Mewtwo on the second airborne frame. The
    // aerial, pressed with jump held on the first frame the float shows,
    // plays inside the float's own status (FitPeach.pac action 0x11E,
    // FitMewtwo.pac action 0x120).
#define TM_FLOAT(aerial) \
    {TM_STEP(X, 0, 0, kRefAbs, 1), TM_UNTIL(X, 0, -100, kRefAbs, kUntilSpecial, 30), aerial, TM_END}
    static const Step kFloatNair[] = TM_FLOAT(TM_STEP(X | A, 0, 0, kRefAbs, 1));
    static const Step kFloatFair[] = TM_FLOAT(TM_STEP(X | C, 100, 0, kRefForward, 1));
    static const Step kFloatBair[] = TM_FLOAT(TM_STEP(X | C, -100, 0, kRefForward, 1));
    static const Step kFloatUair[] = TM_FLOAT(TM_STEP(X | C, 0, 100, kRefAbs, 1));
    static const Step kFloatDair[] = TM_FLOAT(TM_STEP(X | C, 0, -100, kRefAbs, 1));
#undef TM_FLOAT
    // Ganondorf's Dead Man's Float (FitGanon.pac action 0x112, subaction 0x1CE
    // SpecialNFuwa): neutral B for one frame on the first airborne frame
    // (grounded IC-Basic[0] <= 2 or a second Special press branches to 0x117
    // Cape/Reflect, 0x1c880..0x1c8a0). Stick stays neutral for 5 frames until
    // Allow Interrupt opens Air Attack at async frame 5 (0xc798..0xc7b8), then
    // the aerial transitions out of 0x112 into Attack_Air (0x33).
#define TM_GANON_FLOAT(aerial) \
    {TM_TAKEOFF, TM_STEP(B, 0, 0, kRefAbs, 1), TM_STEP(0, 0, 0, kRefAbs, 5), aerial, TM_END}
    static const Step kGanonFloatNair[] = TM_GANON_FLOAT(TM_STEP(A, 0, 0, kRefAbs, 1));
    static const Step kGanonFloatFair[] = TM_GANON_FLOAT(TM_STEP(C, 100, 0, kRefForward, 1));
    static const Step kGanonFloatBair[] = TM_GANON_FLOAT(TM_STEP(C, -100, 0, kRefForward, 1));
    static const Step kGanonFloatUair[] = TM_GANON_FLOAT(TM_STEP(C, 0, 100, kRefAbs, 1));
    static const Step kGanonFloatDair[] = TM_GANON_FLOAT(TM_STEP(C, 0, -100, kRefAbs, 1));
#undef TM_GANON_FLOAT

#undef TM_STEP
#undef TM_UNTIL
#undef TM_END
#undef TM_TAKEOFF

#define TM_NONE {NULL, 0, 0}
#define TM_RANDOM {NULL, kActionRandom, 0}
#define TM_RANDOM_ADV {NULL, kActionRandomAdvanced, 0}
#define TM_CUSTOM_POOL {NULL, kActionCustomPool, 0}
#define TM_DO(steps) {steps, 0, 0}
#define TM_PLAY_SLOTS {NULL, 0, 1}, {NULL, 0, 2}, {NULL, 0, 3}, {NULL, 0, 4}, {NULL, 0, 5}, {NULL, 0, 6}
#define TM_PLAY_RANDOM {NULL, kActionPlayRandomSlot, 0}
#define TM_DJCS                                                                                     \
    {kDjcNair, kForDjc, 0}, {kDjcFair, kForDjc, 0}, {kDjcBair, kForDjc, 0}, {kDjcUair, kForDjc, 0}, \
        {kDjcDair, kForDjc, 0}
#define TM_FLOATS                                                                                       \
    {kFloatNair, kForFloat, 0}, {kFloatFair, kForFloat, 0}, {kFloatBair, kForFloat, 0}, {kFloatUair, kForFloat, 0}, \
        {kFloatDair, kForFloat, 0}
#define TM_SHFFLS                                                                                     \
    {kShfflNair, kForShffl | kEndsWithSteps, 0}, {kShfflFair, kForShffl | kEndsWithSteps, 0},         \
        {kShfflBair, kForShffl | kEndsWithSteps, 0}, {kShfflUair, kForShffl | kEndsWithSteps, 0},     \
        {kShfflDair, kForShffl | kEndsWithSteps, 0}
#define TM_FFLS                                                                                   \
    {kFflNair, kForFfl | kEndsWithSteps, 0}, {kFflFair, kForFfl | kEndsWithSteps, 0},             \
        {kFflBair, kForFfl | kEndsWithSteps, 0}, {kFflUair, kForFfl | kEndsWithSteps, 0},         \
        {kFflDair, kForFfl | kEndsWithSteps, 0}

    // Indexed like the Counter Action rows' labels (ui/pages/cpu_options.cpp).
    // A NULL script is None, Play Slot N (playSlot), or (flags) one of the
    // random kinds. P+ character techniques (DJC, Float) and SHFFL/FFL aerials
    // sit at the end of their tables to preserve all existing indices.
    static const Action kGround[] = {
        TM_NONE,             TM_DO(kSpotdodge),    TM_DO(kShield),        TM_DO(kGrab),
        TM_DO(kUpB),         TM_DO(kSideBToward),  TM_DO(kDownB),         TM_DO(kNeutralB),
        TM_DO(kUSmash),      TM_DO(kDSmash),       TM_DO(kFSmash),        TM_DO(kRollAway),
        TM_DO(kRollToward),  TM_DO(kJab),          TM_DO(kFTilt),         TM_DO(kUTilt),
        TM_DO(kDTilt),       TM_DO(kDashAttack),   TM_DO(kShortHop),      TM_DO(kFullHop),
        TM_DO(kWavedashAway), TM_DO(kWavedashToward), TM_DO(kWavedashDown), TM_DO(kDashBack),
        {kDashThrough, kEndsWithSteps, 0}, TM_PLAY_SLOTS, TM_PLAY_RANDOM, TM_DO(kSideBAway),
        TM_DO(kRollRandom),  TM_DO(kSHNair),       TM_DO(kSHFair),        TM_DO(kSHBair),
        TM_DO(kSHUair),      TM_DO(kSHDair),       TM_RANDOM_ADV,         TM_RANDOM,
        TM_DJCS,             TM_FLOATS,            TM_SHFFLS,             TM_CUSTOM_POOL};
    static const Action kAir[] = {
        TM_NONE,           TM_DO(kAirdodge),        {kJumpAway, kNeedsJump, 0}, {kJumpToward, kNeedsJump, 0},
        {kShortHop, kNeedsJump, 0}, TM_DO(kUpBAway), TM_DO(kUpBToward),         TM_DO(kNeutralB),
        TM_DO(kSideBAway), TM_DO(kSideBToward),     TM_DO(kDownB),              TM_DO(kJab),
        TM_DO(kFair),      TM_DO(kBair),            TM_DO(kUSmash),             TM_DO(kDSmash),
        {kTumbleFastfall, kEndsWithSteps, 0},       {kWiggleFastfall, kEndsWithSteps, 0},
        TM_NONE,           {kWait, kNoTech | kEndsWithSteps, 0},
        TM_PLAY_SLOTS,     TM_PLAY_RANDOM,          TM_RANDOM_ADV,              TM_RANDOM,
        TM_FFLS,           TM_CUSTOM_POOL};
    static const Action kShieldList[] = {
        TM_NONE,              TM_DO(kGrab),           TM_DO(kShortHop),      TM_DO(kFullHop),
        TM_DO(kSpotdodge),    TM_DO(kRollAway),       TM_DO(kRollToward),    TM_DO(kOosUpB),
        TM_DO(kOosSideBAway), TM_DO(kOosSideBToward), TM_DO(kOosDownB),      TM_DO(kOosNeutralB),
        TM_DO(kOosUSmash),    TM_DO(kOosDSmash),      TM_DO(kOosFSmash),     TM_DO(kSHNair),
        TM_DO(kSHFair),       TM_DO(kSHBair),         TM_DO(kSHUair),        TM_DO(kSHDair),
        TM_DO(kWavedashAway), TM_DO(kWait),           TM_PLAY_SLOTS,         TM_PLAY_RANDOM,
        TM_DO(kWavedashToward), TM_DO(kWavedashDown), TM_DO(kRollRandom),    TM_RANDOM_ADV,
        TM_RANDOM,            TM_DJCS,                TM_FLOATS,             TM_SHFFLS,
        TM_CUSTOM_POOL};

#undef TM_NONE
#undef TM_RANDOM
#undef TM_RANDOM_ADV
#undef TM_CUSTOM_POOL
#undef TM_DO
#undef TM_PLAY_SLOTS
#undef TM_PLAY_RANDOM
#undef TM_DJCS
#undef TM_FLOATS
#undef TM_SHFFLS
#undef TM_FFLS

    // Clean P+ Random counter action pools, indexed into the tables above:
    //  - Ground: defensive options, quick pokes, and movement mixups
    //  - Air: airdodge, directional jumps, and the five aerials
    //  - Shield: grab, jumps, dodges, quick OoS attacks, and wavedash
    static const u8 kGroundRandom[] = {1, 2, 3, 11, 12, 13, 14, 15, 16, 18, 20, 23};
    static const u8 kAirRandom[] = {1, 2, 3, 11, 12, 13, 14, 15};
    static const u8 kShieldRandom[] = {1, 2, 4, 5, 6, 7, 12, 15, 16, 20};

    static const Action* const kLists[kListCount] = {kGround, kAir, kShieldList};
    static const int kCounts[kListCount] = {sizeof(kGround) / sizeof(kGround[0]), sizeof(kAir) / sizeof(kAir[0]),
                                            sizeof(kShieldList) / sizeof(kShieldList[0])};

    int actionKind(int list, int index)
    {
        if (list < 0 || list >= kListCount || index <= 0 || index >= kCounts[list])
        {
            return kActionNone;
        }
        const Action& a = kLists[list][index];
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

    static const Action kGanonFloats[] = {
        {kGanonFloatNair, kForFloat, 0}, {kGanonFloatFair, kForFloat, 0}, {kGanonFloatBair, kForFloat, 0},
        {kGanonFloatUair, kForFloat, 0}, {kGanonFloatDair, kForFloat, 0}};

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
        const Action* a = &kLists[list][index];
        if (kind == kKindAny)
        {
            return a;
        }
        if (kind == kGanon && (a->flags & kForFloat) != 0)
        {
            int float0 = index;
            while (float0 > 0 && (kLists[list][float0 - 1].flags & kForFloat) != 0)
            {
                float0--;
            }
            int offset = index - float0;
            return offset >= 0 && offset < 5 ? &kGanonFloats[offset] : NULL;
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
        for (int idx = 0; idx < 64; idx++)
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
