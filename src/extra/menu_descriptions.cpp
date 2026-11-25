#include "extra/menu_descriptions.h"

#include "main/ui/pages/pages.h"

namespace tm {
namespace extra {
    using namespace ui::pages;

    static const char kDescTrainingMenu[] = "Switch directly to Brawl's built-in Training pause menu.";
    static const char kDescExitCss[] = "End this session and return to the Character Select Screen.";
    static const char kDescRevAction[] = "Secondary reversal action mixed with the primary trigger option.";
    static const char kDescRevDelay[] = "Actionable frame delay before the Slot 2 reversal fires.";
    static const char kDescRevChance[] = "Probability weight for Slot 2 versus primary Slot 1.";

    static const char* const kGeneralDesc[] = {
        "Freeze gameplay and step one frame per advance press.",
        "Set Player 1 starting damage percentage immediately.",
        "Keep Player 1 locked at the chosen damage percentage.",
        "Refill P1 shield passively, until hit, or continuously.",
        "Run match gameplay at full speed or slow-motion ratios.",
        "Switch between normal match camera, zoomed, or fixed view.",
        "Toggle P+ stale-move queue tracking for attack damage.",
        "Configure frame advance, D-pad bindings, menu button, and style.",
        "Open diagnostics and frame telemetry overlay toggles.",
        "Restore all Training Lab settings to default values.",
        kDescTrainingMenu,
        kDescExitCss,
        "Show read-only OSDs and info panels in local offline Versus."
    };

    static const char* const kScenariosDesc[] = {
        "Load a preconfigured P+ practice setup with auto-warping.",
        "Automatically reset fighters when the current drill completes.",
        "Choose whether the scenario spawns on the right or left side.",
        "Pick the CPU multi-hit attack for the Multi-Hit SDI preset.",
        "Immediately warp fighters back to the active scenario start.",
        "Configure hitstun auto-reset, recovery looping, and 3D meters."
    };

    static const char* const kCpuDesc[] = {
        "Set the CPU fighter's current damage percentage.",
        "Hold the CPU at its configured damage after every hit.",
        "Use Training Lab AI controls or Brawl's native Training CPU.",
        "Configure tech rolls, getups, traps, and tech invisibility.",
        "Set how the CPU angles survival or combo trajectory DI.",
        "Set how many smash DI shifts the CPU inputs during hitlag.",
        "Choose the direction for CPU hitlag smash DI inputs.",
        "Set the stick direction held at hitlag end for automatic SDI.",
        "Alternate stick angles each hitlag frame for faster CPU SDI.",
        "Set what the CPU does while standing free in neutral.",
        "Choose the CPU reaction after grounded hitstun or knockdown.",
        "Choose the CPU reaction once airborne hitstun ends.",
        "Choose the CPU out-of-shield reaction after blocking a hit.",
        "Wait this many actionable frames before the CPU counters.",
        "Customize unique CPU counter actions for individual combo hits.",
        "Refill CPU shield passively, until hit, or continuously.",
        "Set the target shield health held by CPU Infinite Shield.",
        "Angle the CPU's shield bubble while guarding.",
        "Prevent hits from launching the CPU so you can test moves.",
        "Set how fast the CPU mashes inputs to break out of grabs.",
        "Choose whether grab breaks leave the CPU grounded or airborne.",
        "Reposition the CPU fighter directly on stage with your stick.",
        "Route another controller port directly into the CPU fighter.",
        "Hold the CPU frozen in place without advancing its state.",
        "Select native Game AI, PPlusAI offstage recovery, or Off.",
        "Choose how the CPU gets up after grabbing a stage ledge.",
        "Set CPU mash speed for escaping buries, sleep, and freeze.",
        "Track your habits and adapt CPU counter responses over time.",
        "Automatically L-cancel CPU aerial attacks before landing.",
        "Set aerial and fastfall timing for CPU SHFFL and FFL actions.",
        "Configure weighted multi-slot reversals with per-slot delays.",
        "Post on-screen callouts for 44-fighter P+ signature techs."
    };

    static const char* const kRecordingDesc[] = {
        "Save current fighter states or warp back to saved positions.",
        "Set Player 1 to Off, Record, Playback, or Re-Record inputs.",
        "Choose which P1 input slot records or plays back (or Random).",
        "Control, record, or play back input slots on the CPU fighter.",
        "Choose which CPU input slot records or plays back (or Random).",
        "Flip recorded horizontal stick directions during playback.",
        "Set when the CPU switches from playback back to counter AI.",
        "Restart saved positions and playback automatically when done.",
        "Restore saved positions after playback ends or CPU counters.",
        "Hold playback paused on restore until you begin stepping.",
        "Let P1 or CPU interrupt playback by pressing controller inputs.",
        "Wait to begin recording until the first non-neutral input.",
        "Overwrite the saved recording positions with current states.",
        "Trim leading idle frames from the start of recorded slots.",
        "Clear saved positions and free the recording savestate buffer.",
        "Inspect, edit frame-by-frame, copy, or delete recorded slots.",
        "Set random playback weights and bonus damage for P1 slots.",
        "Set random playback weights and bonus damage for CPU slots.",
        "Save the current setup and recorded input slots to the SD card."
    };

    static const char* const kDisplayDesc[] = {
        "Show or hide Brawl's match timer, damage percents, and tags.",
        "Draw active attack hitboxes and hurtboxes on P1, CPU, or Both.",
        "Accumulate persistent attack coverage overlays across frames.",
        "Draw the outer reach bounding box around the hitbox heatmap.",
        "Adjust transparency of the accumulated hitbox heatmap overlay.",
        "Set how many active frames span each heatmap color band.",
        "Visualize active grabboxes, tether lines, and item catch zones.",
        "Draw real-time launch trajectory, TDI, SDI, and ASDI vectors.",
        "Show or hide 3D character models for P1, CPU, or Both.",
        "Draw stage floor, wall, ceiling, and ledge collision geometry.",
        "Toggle rendering of the 3D stage background and geometry.",
        "Show or hide hit sparks, smoke, and visual particle effects.",
        "Color-code Player 1 fighter states such as IASA, wait, or dash.",
        "Color-code CPU fighter states such as hitstun or shield stun.",
        "Configure live frame and physics telemetry rows for Player 1.",
        "Configure live frame and physics telemetry rows for the CPU.",
        "Show the ledgedash frame timeline bar and GALINT readout.",
        "Customize on-screen notification background color and opacity.",
        "Show live multi-controller sticks, triggers, and buttons.",
        "Color active hitboxes by property or priority ID.",
        "Preview preset DI angles on the live DI compass and arc.",
        "Inspect live PSA AnimCmd threads, wait timers, and FSM."
    };

    static const char* const kOsdDesc[] = {
        "Choose where on-screen notifications stack during practice.",
        "Show wavedash timing, airdodge angle, and short/full hop.",
        "Report L-cancel frame timing, misses, and session success rate.",
        "Display the first frame you act out of shield release.",
        "Report successful, slow, or missed dashback turnarounds.",
        "Show powershield timing or how early a missed press occurred.",
        "Count smash DI shifts completed during each hitlag window.",
        "Alert when an early tech press triggers the tech lockout timer.",
        "Display timing frames for glide tosses and aerial item throws.",
        "Show execution frame timing for boost grabs and DACUS cancels.",
        "Report how quickly you act after landing lag or autocancels.",
        "Show frame timing for aerials, specials, and double jumps.",
        "Report timing frames for jump-canceled grabs and up-smashes.",
        "Show how many frames after apex you triggered a fastfall.",
        "Display frame advantage on shield after hitting a guard.",
        "Show hit count and total damage when a true combo finishes.",
        "Display grab breakout timer and mash rate per frame.",
        "Report ledge hang duration, GALINT frames, and grab limit.",
        "Show how soon you acted after leaving hitstun or tumble.",
        "Display measured frame advantage after landing a clean hit.",
        "Allow extra idle frames when measuring frame advantage.",
        "Limit how many notifications display at once per player.",
        "Show a 3D timing bar above fighters for L-Cancel, WD, or Act-Out."
    };

    static const char* const kStageCastDesc[] = {
        "Swap P1 or CPU fighters and costumes mid-match without CSS.",
        "Expand all stage and fighter rows even when not in this match.",
        "Override dynamic stage hazards to Default, On, or Off.",
        "Cycle, freeze, or lock moving stage platforms to a preset.",
        "Set the Fountain of Dreams left platform to a fixed height.",
        "Set the Fountain of Dreams right platform to a fixed height.",
        "Force Peach vegetable pulls to a specific turnip face or item.",
        "Lock Peach forward smash to Golf Club, Pan, or Racket.",
        "Keep Peach float available without needing to re-land.",
        "Force Luigi Green Missile to misfire Always, Never, or Default.",
        "Force Mr. Game & Watch Judge hammer to a fixed number (1-9).",
        "Lock Mr. Game & Watch Oil Panic bucket fill level.",
        "Hold Samus Charge Shot at Empty, Half, or Full charge.",
        "Lock Samus active beam mode to Plasma or Ice.",
        "Allow unlimited Samus aerial bombs without landing resets.",
        "Hold Mewtwo Shadow Ball at Empty, Half, or Full charge.",
        "Grant Mewtwo unlimited hover duration without landing resets.",
        "Hold Lucario stored Aura charges at 0, 1, or 2.",
        "Hold Lucario Aura Sphere at Empty or Full charge.",
        "Hold Wario Waft charge at Empty, Half, or Full.",
        "Hold Donkey Kong Giant Punch at Empty or Full charge.",
        "Hold Ivysaur Synthesis charge at Empty, Half, or Full.",
        "Grant R.O.B. unlimited aerial Robo Burner fuel boosts.",
        "Hold R.O.B. Gyro at Empty or Full spin charge.",
        "Lock R.O.B. Robo Beam laser charge to Uncharged, Normal, Super.",
        "Lock Lucas Offense Up buff to Off or Charged.",
        "Hold Squirtle Water Gun at Empty or Full charge.",
        "Lock Sheik stored needle count to 0 or 6.",
        "Force King Dedede side special to Waddle Dee, Doo, or Gordo.",
        "Keep Snake Tranquilizer ammo stocked with unlimited shots.",
        "Allow Snake unlimited Cypher uses without landing resets.",
        "Grant Ganondorf unlimited aerial float duration.",
        "Force Ice Climbers Nana grab throws in a chosen direction."
    };

    static const char* const kControlsDesc[] = {
        "Choose the button that steps one frame during Frame Advance.",
        "Frames to hold Frame Advance before gameplay runs continuously.",
        "Bind a button to step backward through recent match frames.",
        "Keep D-Pad Up as Taunt or disable it during Training Lab.",
        "Leave D-Pad Down normal or bind it to toggle Frame Advance.",
        "Bind D-Pad Left to load the quick savestate or disable it.",
        "Bind D-Pad Right to save a quick savestate or disable it.",
        "Open Training Lab directly with Start or with Z in Pause.",
        "Allow L+R+D-Pad Down to open the P+ Code Menu in Training.",
        "Switch live between Category Rail and Top Tabs layout."
    };

    static const char* const kDebugDesc[] = {
        "Show frame counters, scene info, and advantage debug text."
    };

    static const char* const kComboTrainerDesc[] = {
        "Enable automatic combo reset tracking and 3D state meters.",
        "Temporarily disable stage clipping on reset for clean warps.",
        "Return the CPU to its anchor point when hitstun ends.",
        "Move the CPU to a random safe stage spot on each reset.",
        "Constrain random combo resets to grounded floor and platforms.",
        "Assign a random 0-99% CPU damage value on every reset.",
        "Set the fixed damage applied to the CPU on every reset.",
        "Shift how early or late hitstun expiration triggers a reset.",
        "Loop offstage CPU spawns so you can practice edgeguards.",
        "Choose which 3D floating state meters render above fighters."
    };

    static const char* const kComboMetersDesc[] = {
        "Show remaining hitlag freeze frames above the fighter.",
        "Show remaining hitstun frames as a 3D meter during combos.",
        "Show remaining shieldstun frames above a shielding fighter.",
        "Render the 3D directional influence vector ring during hitlag.",
        "Highlight remaining frames until interruptible action opens.",
        "Show remaining ledge invulnerability frames above the fighter.",
        "Show remaining dodge, respawn, or move intangibility frames."
    };

    static const char* const kTechDesc[] = {
        "Choose how the CPU techs knockdowns (or Random weights).",
        "Choose the CPU wakeup action after a missed tech.",
        "Hide fighter models briefly during tech roll animations.",
        "Delay tech invisibility by this many distinguishable frames.",
        "Play a sound cue when the CPU commits to a tech option.",
        "Make the CPU input early or late tech presses to test traps.",
        "Choose early or late timing inside the CPU tech window.",
        "Weight for neutral tech in place when Tech is set to Random.",
        "Weight for tech roll away when Tech is set to Random.",
        "Weight for tech roll toward when Tech is set to Random.",
        "Weight for missed tech knockdown when Tech is set to Random.",
        "Chance for the CPU to stay lying down before waking up.",
        "Weight for neutral getup when Getup is set to Random.",
        "Weight for roll away wakeup when Getup is set to Random.",
        "Weight for roll toward wakeup when Getup is set to Random.",
        "Weight for getup attack when Getup is set to Random."
    };

    static const char* const kAdvCounterDesc[] = {
        "Select which hit number in a combo (1-10) to customize.",
        "Use Default counter, turn Off, or set Custom actions here.",
        "Custom grounded counter action for the selected hit index.",
        "Custom airborne counter action for the selected hit index.",
        "Custom out-of-shield counter action for the selected hit.",
        "Actionable frame delay before the custom grounded counter.",
        "Actionable frame delay before the custom airborne counter.",
        "Actionable frame delay before the custom shield counter."
    };

    static const char* const kSlotsDesc[] = {
        "Choose whether to manage Player 1 or CPU recording slots.",
        "Select which recording slot (1-6) to inspect or modify.",
        "Open the frame-by-frame stick and button input editor.",
        "Erase all recorded frames from the selected input slot.",
        "Select the destination slot number for copying inputs.",
        "Duplicate the selected slot's inputs into the destination."
    };

    static const char* const kAlterDesc[] = {
        "Select which recorded frame of the slot to edit.",
        "Set main control stick horizontal value (-80 to 80).",
        "Set main control stick vertical value (-80 to 80).",
        "Set C-Stick horizontal value (-80 to 80) for this frame.",
        "Set C-Stick vertical value (-80 to 80) for this frame.",
        "Set left analog trigger depth (0 to 150) for this frame.",
        "Set right analog trigger depth (0 to 150) for this frame.",
        "Toggle the Attack button input on this recorded frame.",
        "Toggle the Special button input on this recorded frame.",
        "Toggle the Jump button input on this recorded frame.",
        "Toggle the Shield button input on this recorded frame.",
        "Toggle the Smash flag on this recorded frame.",
        "Toggle the C-Stick button flag on this recorded frame.",
        "Toggle Up Taunt input on this recorded frame.",
        "Toggle Side Taunt input on this recorded frame.",
        "Toggle Down Taunt input on this recorded frame."
    };

    static const char* const kSlotChancesDesc[] = {
        "Selection weight for Slot 1 during random slot playback.",
        "Selection weight for Slot 2 during random slot playback.",
        "Selection weight for Slot 3 during random slot playback.",
        "Selection weight for Slot 4 during random slot playback.",
        "Selection weight for Slot 5 during random slot playback.",
        "Selection weight for Slot 6 during random slot playback.",
        "Add up to this much random damage on each restore or load."
    };

    static const char* const kOverlaysDesc[] = {
        "Choose Custom state colors or apply the ProjectPunch preset.",
        "Overlay color or cue while the fighter is free to act.",
        "Overlay color or cue while the fighter is in hitstun.",
        "Overlay color or cue during invincibility or intangibility.",
        "Overlay color or cue when actionable while hanging on ledge.",
        "Overlay color or cue after missing an aerial L-cancel.",
        "Overlay color or cue once aerial fastfall becomes available.",
        "Overlay color or cue during an aerial autocancel window.",
        "Overlay color or cue while the fighter is crouching.",
        "Overlay color or cue while standing idle in Wait.",
        "Overlay color or cue while walking on the ground.",
        "Overlay color or cue during the initial dash animation.",
        "Overlay color or cue while running across the stage.",
        "Overlay color or cue during a full-hop jump arc.",
        "Overlay color or cue during a short-hop jump arc.",
        "Overlay color or cue once a move reaches its IASA frames.",
        "Overlay color or cue while locked in shield stun."
    };

    static const char* const kInfoDesc[] = {
        "Fill all 8 info rows from a quick telemetry preset.",
        "Choose Small, Medium, or Large text for this info panel.",
        "Pick the live frame or physics readout shown in Row 1.",
        "Pick the live frame or physics readout shown in Row 2.",
        "Pick the live frame or physics readout shown in Row 3.",
        "Pick the live frame or physics readout shown in Row 4.",
        "Pick the live frame or physics readout shown in Row 5.",
        "Pick the live frame or physics readout shown in Row 6.",
        "Pick the live frame or physics readout shown in Row 7.",
        "Pick the live frame or physics readout shown in Row 8."
    };

    static const char* const kLedgedashDesc[] = {
        "Show the frame-by-frame ledgedash timing bar after ledge drops.",
        "Set how many frame cells the ledgedash timeline displays.",
        "Color grounded actionable invulnerable frames on the bar.",
        "Display the numeric GALINT, ledge frames, and angle strip."
    };

    static const char* const kFighterSwapDesc[] = {
        "Select a new character to load into Player 1 mid-match.",
        "Select the costume color index for the swapped Player 1.",
        "Select a new character to load into the CPU mid-match.",
        "Select the costume color index for the swapped CPU.",
        "Reload the chosen fighters in place without leaving the match."
    };

    static const char* const kThemeDesc[] = {
        "Choose the background color preset for OSD notification cards.",
        "Adjust OSD card opacity from transparent (0%) to solid (100%).",
        "Choose the chrome accent palette for the in-match Training Lab.",
        "Choose the background color preset for Info Display panels.",
        "Adjust Info Display opacity with drop shadow at low values."
    };

    static const char* const kReversalDesc[] = {
        "Enable weighted multi-slot reversals across all four triggers.",
        "Choose On-Hit, On-Shield, On-Getup, or On-Ledge slots to edit.",
        kDescRevAction,
        kDescRevDelay,
        kDescRevChance,
        kDescRevAction,
        kDescRevDelay,
        kDescRevChance,
        kDescRevAction,
        kDescRevDelay,
        kDescRevChance,
        kDescRevAction,
        kDescRevDelay,
        kDescRevChance
    };

    static const char* const kRailDesc[] = {
        "Frame advance, Player 1 damage, speed, camera, and controls.",
        "One-click P+ practice presets and Combo Trainer settings.",
        "CPU damage, DI, SDI, tech habits, counters, and recovery AI.",
        "Record, edit, loop, mirror, and export multi-slot inputs.",
        "Hitboxes, heatmaps, DI vectors, overlays, and info panels.",
        "Toggle on-screen frame data and technique notifications.",
        "Fighter swap, active stage options, and character mechanics.",
        "Press A twice to reset all Training Lab settings to defaults.",
        kDescTrainingMenu,
        kDescExitCss
    };

    struct DescTable {
        const char* const* rows;
        u8 count;
    };

#define TM_DESC(a) {a, static_cast<u8>(sizeof(a) / sizeof((a)[0]))}
    // Indexed by ui::pages::PageId, plus P_COUNT for Category Rail items.
    static const DescTable kTables[P_COUNT + 1] = {
        TM_DESC(kGeneralDesc),      // P_GENERAL
        TM_DESC(kScenariosDesc),    // P_SCENARIOS
        TM_DESC(kCpuDesc),          // P_CPU
        TM_DESC(kRecordingDesc),    // P_RECORDING
        TM_DESC(kDisplayDesc),      // P_DISPLAY
        TM_DESC(kOsdDesc),          // P_OSD_MENU
        TM_DESC(kStageCastDesc),    // P_STAGE_CAST
        TM_DESC(kControlsDesc),     // P_CONTROLS
        TM_DESC(kDebugDesc),        // P_DEBUG
        TM_DESC(kComboTrainerDesc), // P_COMBO_TRAINER
        TM_DESC(kComboMetersDesc),  // P_COMBO_METERS
        TM_DESC(kTechDesc),         // P_TECH
        TM_DESC(kAdvCounterDesc),   // P_ADVANCED_COUNTER
        TM_DESC(kSlotsDesc),        // P_SLOTS
        TM_DESC(kAlterDesc),        // P_ALTER_INPUTS
        TM_DESC(kSlotChancesDesc),  // P_HMN_CHANCES
        TM_DESC(kSlotChancesDesc),  // P_CPU_CHANCES
        TM_DESC(kOverlaysDesc),     // P_HMN_OVERLAYS
        TM_DESC(kOverlaysDesc),     // P_CPU_OVERLAYS
        TM_DESC(kInfoDesc),         // P_HMN_INFO
        TM_DESC(kInfoDesc),         // P_CPU_INFO
        TM_DESC(kLedgedashDesc),    // P_LEDGEDASH_HUD
        TM_DESC(kFighterSwapDesc),  // P_FIGHTER_SWAP
        TM_DESC(kThemeDesc),        // P_THEME
        TM_DESC(kReversalDesc),     // P_REVERSALS
        TM_DESC(kRailDesc)          // P_COUNT (Category Rail entries)
    };
#undef TM_DESC

    const char* optionDesc(u8 pageId, u8 row)
    {
        if (pageId > P_COUNT)
        {
            return NULL;
        }
        const DescTable& t = kTables[pageId];
        return row < t.count ? t.rows[row] : NULL;
    }
}
}
