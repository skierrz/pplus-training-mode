#!/usr/bin/env python3
"""Generate LZ77-compressed src/extra/menu_descriptions.cpp from readable tables.

Usage:
  python scripts/gen_menu_descriptions.py          # regenerate menu_descriptions.cpp
  python scripts/gen_menu_descriptions.py --check  # verify file is up to date
"""
import pathlib
import sys

DESC_TRAINING_MENU = "Switch directly to Brawl's built-in Training pause menu."
DESC_EXIT_CSS = "End this session and return to the Character Select Screen."
DESC_INFINITE_SHIELD = "Off (normal), Until Hit (full until struck), or On (always full)."
DESC_REV_ACTION = "Secondary reversal action mixed with the primary trigger option."
DESC_REV_DELAY = "Actionable frame delay before the Slot 2 reversal fires."
DESC_REV_CHANCE = "Probability weight for Slot 2 versus primary Slot 1."
DESC_TECH_INVIS = "Hide models during techs, or Flash only on the tell frame."
DESC_TECH_DELAY = "Frames after the distinguishable tech frame before hiding model."
DESC_TECH_SOUND = "Play a sound cue when the CPU commits to a tech option."
DESC_TECH_DRIFT = "Drift horizontally in tumble before landing to mix up spacing."

TABLES = {
    "kGeneralDesc": [
        "Freeze gameplay and step one frame per advance press.",
        "Set P1 damage (Left/Right) or press A to reset to 0%.",
        "Keep Player 1 locked at the chosen damage percentage.",
        "Add up to this much random P1 damage on each savestate load.",
        DESC_INFINITE_SHIELD,
        "Run match gameplay at full speed or slow-motion ratios.",
        "Switch between normal match camera, zoomed, or fixed view.",
        "Toggle P+ stale-move queue tracking for attack damage.",
        "Show read-only OSDs, Info, Ledgedash HUD, and PSA in Offline VS.",
        "Configure frame advance, D-pad bindings, menu button, and style.",
        "Restore all Training Lab settings to default values.",
        DESC_TRAINING_MENU,
        DESC_EXIT_CSS,
    ],
    "kScenariosDesc": [
        "Load a P+ practice scenario with auto-resetting positions.",
        "Auto-reset fighters when the drill completes.",
        "Right, Left, or Random spawn side.",
        "CPU escape action during the post-hitstun Combo Lab reset window.",
        "Edit the Air Custom Counter Pool used when Combo Lab escapes.",
        "Wait this long after hitstun ends before warping (hits cancel timer).",
        "Set how the CPU angles survival, combo, or custom trajectory DI.",
        "Choose how the CPU techs knockdowns (or Random weights).",
        "Restore Player 1 position and starting damage on every combo reset.",
        "Temporarily disable stage clipping on reset for clean warps.",
        "Start Techchase via Tumble Drop or Free Hit.",
        "Ground or Platform 1-4 (D-Pad Up/Down steps).",
        "CPU escape action on wakeup, or Intangible Flash.",
        "Show reaction frames past tell frame on HUD.",
        DESC_TECH_INVIS,
        DESC_TECH_DELAY,
        DESC_TECH_SOUND,
        DESC_TECH_DRIFT,
        "CPU launch and throw DI for Techchase.",
        "Start Edgeguard via Launch or Offstage Spawn.",
        "Knockback launch strength for Edgeguard.",
        "Offstage spawn distance for Edgeguard.",
        "Offstage CPU double jump available or spent.",
        "PPlusAI recovery profile (Mixed/Low/High/Direct).",
        "Reset Edgeguard on safe onstage landing or on ledge grab.",
        "Switch between Shield Pressure Offense and Out-of-Shield Defense.",
        "CPU Out-of-Shield punish option (including Custom Pool).",
        "Edit the Shield Custom Counter Pool used for CPU OoS counters.",
        "CPU attack to block and punish Out of Shield.",
        "Start P1 on Revival Platform or Instant Drop.",
        "Hold Active freezes hitbox at ledge, or Timed.",
        "Offstage spawn position for Ledgetech.",
        "P1 double jump available or spent.",
        "CPU ledge-poke attack for Ledgetech.",
        "Left, Right, Top, or Random platform (Up cycles).",
        "Active CPU shield pressure below platform, or Off.",
        "CPU spacing below Shield Drop platform.",
        "Shield Drop CPU facing toward or away.",
        "Start P1 Knocked Down, Tumbling, or Standing on platform.",
        "Under-platform aerial or on-platform ftilt.",
        "Hold (Mash Out), Throw DI, or Stand for Grab Defense.",
        "Random 0-79% or Current P1 % on Grab resets.",
        "Auto (best for CPU fighter) or specific multi-hit move.",
        "Warp fighters back to the active scenario start.",
        "L+D-Up or menu: switch Free, 10-Rep, and 20-Rep practice.",
        "Powershield CPU attack: Neutral B projectile or physical jab.",
        "Short-hop projectile height (Low, Mid, High, Ground, or Random).",
        "Delay between Powershield CPU attacks (Slow to Random).",
        "Which ledge Ledgedash snaps onto (in-match D-Right flips side).",
        "Auto-snap P1 back to ledge after each ledgedash or SD.",
        "Enforce P+ 5-grab ledge invuln limit or grant unlimited refresh.",
        "CPU target for L-Cancel hitlag practice (Off to Moving).",
        "CPU target for Float/Hover/DJC aerials (Off to Moving).",
        "Show the GX Wavedash Target Zone ribbon and streak tracker.",
        "Hitstun auto-reset, recovery looping, and 3D meters.",
        "Launch the P+ Snake C4 Recovery character drill.",
        "Launch the P+ Float / Hover & DJC character drill.",
        "Exit the active practice drill and return to Training Lab.",
    ],
    "kCpuDesc": [
        "Set CPU damage (Left/Right) or press A to reset to 0%.",
        "Hold the CPU at its configured damage after every hit.",
        "Add up to this much random CPU damage on each savestate load.",
        "Use Training Lab AI controls or Brawl's native Training CPU.",
        "Set what the CPU does while standing free in neutral.",
        "Configure CPU trajectory DI, hitlag smash DI, and ASDI.",
        "Configure ground/air/shield counters, reversals, and SHFFL.",
        "Configure tech rolls, getups, traps, and tech invisibility.",
        "Configure CPU shield regen, tilt, no-knockback, and mash.",
        "Select native Game AI, PPlusAI offstage recovery, or Off.",
        "Choose how the CPU gets up after grabbing a stage ledge.",
        "Reposition the CPU fighter directly on stage with your stick.",
        "Hold the CPU frozen in place without advancing its state.",
        "Route another controller port directly into the CPU fighter.",
    ],
    "kRecordingDesc": [
        "Save current fighter states or warp back to saved positions.",
        "Set Player 1 to Off, Record, Playback, or Re-Record inputs.",
        "Choose which P1 input slot records or plays back (or Random).",
        "Control, record, or play back input slots on the CPU fighter.",
        "Choose which CPU input slot records or plays back (or Random).",
        "Restart saved positions and playback automatically when done.",
        "Restore saved positions after playback ends or CPU counters.",
        "Inspect, edit, copy, delete, or weight saved recording slots.",
        "Configure mirrored playback, counters, start options, and SD export.",
    ],
    "kDisplayDesc": [
        "Show live dual-fighter frame strip and choose who sits on top.",
        "Show live multi-controller sticks, triggers, and buttons.",
        "Draw active attack hitboxes and hurtboxes on P1, CPU, or Both.",
        "Color active hitboxes by property or priority ID.",
        "Draw real-time launch trajectory, TDI, SDI, and ASDI vectors.",
        "Preview preset DI angles on the live DI compass and arc.",
        "Configure hitbox heatmaps, grab/tether boxes, and stage collision.",
        "Configure P1/CPU state overlays, floating meters, and ledgedash HUD.",
        "Customize Menu Accent, OSD, and Info Display colors and opacity.",
        "Info Displays, PSA Debugger, Debug Overlay, HUD, and model toggles.",
    ],
    "kOsdDesc": [
        "Apply a competitive OSD bundle or load a saved Custom slot.",
        "Choose which Custom slot (1-3) receives your saved OSD layout.",
        "Save all current OSD toggle states into the selected Custom slot.",
        "Choose where OSDs stack (Sides uses left/right edges in 1v1).",
        "Limit how many notifications display at once per player.",
        "3D timing bar above fighters (independent of text OSD toggles).",
        "Show a native sparkle cue on Perfect Wavedash or Shorthop.",
        "Toggle individual frame data and technique OSD readouts.",
    ],
    "kOsdIndividualDesc": [
        "Show wavedash timing, airdodge angle, and short/full hop.",
        "Report L-cancel frame timing, misses, and session success rate.",
        "Report ledge hang, GALINT (grounded ledge invuln), and 5-grab limit.",
        "Display the first frame you act Out of Shield (OoS).",
        "Display measured frame advantage on shield after hitting guard.",
        "Display measured frame advantage after landing a clean hit.",
        "Allow extra idle frames when measuring frame advantage.",
        "Report successful, slow, or missed dashback turnarounds.",
        "Show powershield timing or how early a missed press occurred.",
        "Report how quickly you act after landing lag or autocancels.",
        "Show frame timing for aerials, specials, and double jumps.",
        "Show how soon you acted after leaving hitstun or tumble.",
        "Report timing frames for jump-canceled grabs and up-smashes.",
        "Show how many frames after apex you triggered a fastfall.",
        "Show timing for boost grabs and Dash-Attack Canceled Up Smash.",
        "Count smash DI shifts completed during each hitlag window.",
        "Alert when an early tech press triggers the tech lockout timer.",
        "Show hit count and total damage when a true combo finishes.",
        "Display grab breakout timer and mash rate per frame.",
        "Display timing frames for glide tosses and aerial item throws.",
        "Post on-screen callouts for 44-fighter P+ signature techs.",
    ],
    "kRecManageDesc": [
        "Flip playback left/right (locks P1/CPU mode and slot rows).",
        "Set when the CPU switches from playback back to counter AI.",
        "Hold playback frozen on restore until any live controller input.",
        "Let P1 or CPU interrupt playback by pressing controller inputs.",
        "Wait to begin recording until the first non-neutral input.",
        "Overwrite the saved recording positions with current states.",
        "Trim frames before the current playback frame and re-save start.",
        "Press A twice to clear saved positions and recording slots.",
        "Save the current setup and recorded input slots to the SD card.",
    ],
    "kCollisionDesc": [
        "Accumulate persistent attack coverage overlays across frames.",
        "Draw the outer reach bounding box around the hitbox heatmap.",
        "Adjust transparency of the accumulated hitbox heatmap overlay.",
        "Set how many active frames span each heatmap color band.",
        "Visualize active grabboxes, tether lines, and item catch zones.",
        "Draw stage floor, wall, ceiling, and ledge collision geometry.",
    ],
    "kHudOverlaysDesc": [
        "Color-code P1 fighter states (lower rows override upper rows).",
        "Color-code CPU fighter states (lower rows override upper rows).",
        "Choose which 3D floating state meters render above fighters.",
        "Show ledgedash timeline bar and GALINT (ledge invuln) readout.",
    ],
    "kStageCastDesc": [
        "Swap P1 or CPU fighters and costumes mid-match without CSS.",
        "Show inactive stage and fighter rows on Stage & Cast.",
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
        "Grant R.O.B. unlimited aerial Robo Burner boosts.",
        "Hold R.O.B. Gyro at Empty or Full spin charge.",
        "Lock R.O.B. Robo Beam laser charge to Uncharged, Normal, Super.",
        "Lock Lucas Offense Up buff to Off or Charged.",
        "Hold Squirtle Water Gun at Empty or Full charge.",
        "Lock Sheik stored needle count to 0 or 6.",
        "Force King Dedede side special to Waddle Dee, Doo, or Gordo.",
        "Keep Snake Tranquilizer ammo stocked with unlimited shots.",
        "Allow Snake unlimited Cypher uses without landing resets.",
        "Grant Ganondorf unlimited aerial float duration.",
        "Force Ice Climbers Nana grab throws in a chosen direction.",
        "Configure Olimar pull pool, maturity, lineup, whistle, and presets.",
    ],
    "kControlsDesc": [
        "Choose the button that steps one frame during Frame Advance.",
        "Frames to hold Advance or Rewind before repeating continuously.",
        "Step back a frame (needs Frame Advance + Recording Playback).",
        "Keep D-Pad Up as Taunt or disable it during Training Lab.",
        "Leave D-Pad Down normal or bind it to toggle Frame Advance.",
        "Bind D-Pad Left to load the quick savestate or disable it.",
        "Hold D-Pad Right (0.2s) to save a quick savestate, or Off.",
        "Open Training Lab directly with Start or with Z in Pause.",
        "Allow L+R+D-Pad Down to open the P+ Code Menu in Training.",
        "Switch live between Category Rail and Top Tabs layout.",
    ],
    "kDebugDesc": [
        "Configure live frame and physics telemetry rows for Player 1.",
        "Configure live frame and physics telemetry rows for the CPU.",
        "For modding/debugging (not normal training): PSA threads & FSM.",
        "Show frame counters, scene info, and advantage debug text.",
        "Show or hide Brawl's match timer, damage percents, and tags.",
        "Show or hide 3D character models for P1, CPU, or Both.",
        "Toggle rendering of the 3D stage background and geometry.",
        "Show or hide hit sparks, smoke, and visual particle effects.",
    ],
    "kComboTrainerDesc": [
        "Enable automatic combo reset tracking and recovery looping.",
        "Temporarily disable stage clipping on reset for clean warps.",
        "Return the CPU to its anchor point when hitstun ends.",
        "Restore Player 1 position and starting damage on every combo reset.",
        "Wait this long after hitstun ends before warping (hits cancel timer).",
        "CPU escape action during the post-hitstun Combo Lab reset window.",
        "Open Trajectory DI, Custom 2D DI Angle, SDI, and ASDI options.",
        "Move the CPU to a random safe stage spot on each reset.",
        "Constrain random combo resets to grounded floor and platforms.",
        "Assign a random 0-99% CPU damage value on every reset.",
        "Use CPU Damage or a custom percentage when resets warp the CPU.",
        "Set the custom damage applied to the CPU on every reset.",
        "Fine-tune frames added to the post-hitstun reset window.",
        "Loop offstage CPU spawns with PPlusAI for edgeguard drills.",
        "Bias PPlusAI offstage recovery mixups (Mixed, Low, High, Direct).",
    ],
    "kComboMetersDesc": [
        "Show remaining hitlag freeze frames above the fighter.",
        "Show remaining hitstun frames as a 3D meter during combos.",
        "Show remaining shieldstun frames above a shielding fighter.",
        "Render the 3D directional influence vector ring during hitlag.",
        "Highlight remaining frames until interruptible action opens.",
        "Show remaining ledge invulnerability frames above the fighter.",
        "Show remaining dodge, respawn, or move intangibility frames.",
    ],
    "kTechDesc": [
        "Choose how the CPU techs knockdowns (or Random weights).",
        "Choose the CPU wakeup action after a missed tech.",
        DESC_TECH_INVIS,
        DESC_TECH_DELAY,
        DESC_TECH_SOUND,
        "Simulate a bad mid-air press to test early/late tech lockouts.",
        "Choose early or late timing inside the valid CPU tech window.",
        DESC_TECH_DRIFT,
        "Configure random tech, wakeup, and missed-tech wait weights.",
    ],
    "kTechWeightsDesc": [
        "Weight for neutral tech in place when Tech is set to Random.",
        "Weight for tech roll away when Tech is set to Random.",
        "Weight for tech roll toward when Tech is set to Random.",
        "Weight for missed tech knockdown when Tech is set to Random.",
        "Chance for the CPU to stay lying down before waking up.",
        "Weight for neutral getup when Getup is set to Random.",
        "Weight for roll away wakeup when Getup is set to Random.",
        "Weight for roll toward wakeup when Getup is set to Random.",
        "Weight for getup attack when Getup is set to Random.",
    ],
    "kAdvCounterDesc": [
        "Select which hit number in a combo (1-10) to customize.",
        "Use Default counter, turn Off, or set Custom actions here.",
        "Custom grounded counter action for the selected hit index.",
        "Custom airborne counter action for the selected hit index.",
        "Custom out-of-shield counter action for the selected hit.",
        "Actionable frame delay before the custom grounded counter.",
        "Actionable frame delay before the custom airborne counter.",
        "Actionable frame delay before the custom shield counter.",
    ],
    "kSlotsDesc": [
        "Choose whether to manage Player 1 or CPU recording slots.",
        "Select which recording slot (1-6) to inspect or modify.",
        "Open the frame-by-frame stick and button input editor.",
        "Press A twice to erase all recorded frames from this slot.",
        "Select the destination slot number for copying inputs.",
        "Duplicate the selected slot's inputs into the destination.",
        "Set random playback weights and bonus damage for P1 slots.",
        "Set random playback weights and bonus damage for CPU slots.",
    ],
    "kAlterDesc": [
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
        "Toggle Down Taunt input on this recorded frame.",
    ],
    "kSlotChancesDesc": [
        "Selection weight for Slot 1 during random slot playback.",
        "Selection weight for Slot 2 during random slot playback.",
        "Selection weight for Slot 3 during random slot playback.",
        "Selection weight for Slot 4 during random slot playback.",
        "Selection weight for Slot 5 during random slot playback.",
        "Selection weight for Slot 6 during random slot playback.",
        "Add up to this much random damage on each restore or load.",
    ],
    "kOverlaysDesc": [
        "Preset or Custom colors (lower rows override upper rows).",
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
        "Overlay color or cue while locked in shield stun.",
    ],
    "kInfoDesc": [
        "Fill all 8 info rows from a quick telemetry preset.",
        "Choose Small, Medium, or Large text for this info panel.",
        "Pick the live frame or physics readout shown in Row 1.",
        "Pick the live frame or physics readout shown in Row 2.",
        "Pick the live frame or physics readout shown in Row 3.",
        "Pick the live frame or physics readout shown in Row 4.",
        "Pick the live frame or physics readout shown in Row 5.",
        "Pick the live frame or physics readout shown in Row 6.",
        "Pick the live frame or physics readout shown in Row 7.",
        "Pick the live frame or physics readout shown in Row 8.",
    ],
    "kLedgedashDesc": [
        "Show the frame-by-frame ledgedash timing bar after ledge drops.",
        "Set how many frame cells the ledgedash timeline displays.",
        "Color grounded actionable invulnerable frames on the bar.",
        "Display the numeric GALINT, ledge frames, and angle strip.",
    ],
    "kFighterSwapDesc": [
        "Select a new character to load into Player 1 mid-match.",
        "Select the costume color index for the swapped Player 1.",
        "Select a new character to load into the CPU mid-match.",
        "Select the costume color index for the swapped CPU.",
        "Reload the chosen fighters in place without leaving the match.",
    ],
    "kThemeDesc": [
        "Choose the background color preset for OSD notification cards.",
        "Adjust OSD card opacity from transparent (0%) to solid (100%).",
        "Choose the chrome accent palette for the in-match Training Lab.",
        "Choose the background color preset for Info Display panels.",
        "Adjust Info Display opacity with drop shadow at low values.",
    ],
    "kReversalDesc": [
        "Enable weighted multi-slot reversals across all four triggers.",
        "Choose On-Hit, On-Shield, On-Getup, or On-Ledge slots to edit.",
        "Primary Slot 1 reversal configured in CPU Counters / Tech / Ledge.",
        DESC_REV_ACTION,
        DESC_REV_DELAY,
        DESC_REV_CHANCE,
        DESC_REV_ACTION,
        DESC_REV_DELAY,
        DESC_REV_CHANCE,
        DESC_REV_ACTION,
        DESC_REV_DELAY,
        DESC_REV_CHANCE,
        DESC_REV_ACTION,
        DESC_REV_DELAY,
        DESC_REV_CHANCE,
    ],
    "kCpuDiDesc": [
        "Set how the CPU angles survival, combo, or custom trajectory DI.",
        "Tilt C-Stick to aim DI live, or press A for Control Stick aim.",
        "Set how many smash DI shifts the CPU inputs during hitlag.",
        "Choose the direction for CPU hitlag smash DI inputs.",
        "Alternate stick angles each hitlag frame for faster CPU SDI.",
        "Stick direction held at hitlag end for Automatic Smash DI (ASDI).",
    ],
    "kCpuCountersDesc": [
        "Choose Off, Single, Custom Pool, Reversals, Per-Hit, or Adaptive.",
        "Choose the CPU reaction after grounded hitstun or knockdown.",
        "Choose the CPU reaction once airborne hitstun ends.",
        "Choose the CPU out-of-shield reaction after blocking a hit.",
        "Counter on First Gap or hold shield through a Full Move.",
        "Actionable frames before CPU counters or ledge getups fire.",
        "Toggle specific Ground, Air, and Shield moves for Custom Pool.",
        "Customize unique CPU counter actions for individual combo hits.",
        "Configure weighted multi-slot reversals with per-slot delays.",
        "Track P1 habits with live HUD, or also bias CPU counter options.",
        "Automatically L-cancel CPU aerial attacks before landing.",
        "Set aerial and fastfall timing for CPU SHFFL and FFL actions.",
    ],
    "kCpuDefenseDesc": [
        DESC_INFINITE_SHIELD,
        "Set the target shield health held by CPU Infinite Shield (60 max).",
        "Angle the CPU's shield bubble while guarding.",
        "Prevent hits from launching the CPU (locks CPU damage to 0%).",
        "Set how fast the CPU mashes inputs to break out of grabs.",
        "Choose whether grab breaks leave the CPU grounded or airborne.",
        "Set CPU mash rate to escape bury, freeze, sleep, and status traps.",
    ],
    "kCounterPoolDesc": [
        "Choose Ground, Air, or Shield Custom Counter Pool to edit.",
        "Shows if this pool is active; press A to enable for this trigger.",
        "Restore the curated default moves for this counter pool.",
        "Clear all moves in this counter pool.",
        "Include a no-counter wait chance in this Custom Counter Pool.",
    ] + ["Toggle this move in the active Custom Counter Pool."] * 52 + [
        "Include a no-counter wait chance in this Custom Counter Pool.",
    ] + ["Toggle this move in the active Custom Counter Pool."] * 28 + [
        "Include a no-counter wait chance in this Custom Counter Pool.",
    ] + ["Toggle this move in the active Custom Counter Pool."] * 39,
    "kOlimarDesc": [
        "Use D-pad for live B+D-Pad plucking or instant lineup presets.",
        "Allow Red Pikmin in Olimar's random Neutral-B pluck pool.",
        "Allow Yellow Pikmin in Olimar's random Neutral-B pluck pool.",
        "Allow Blue Pikmin in Olimar's random Neutral-B pluck pool.",
        "Allow White Pikmin in Olimar's random Neutral-B pluck pool.",
        "Allow Purple Pikmin in Olimar's random Neutral-B pluck pool.",
        "Force Down-B whistle and Move Lead Now to place this color first.",
        "Immediately move the selected color to lead (or cycle if Default).",
        "Set maturity lock for all 5 Pikmin colors at once.",
        "Lock Red Pikmin maturity to Default, Leaf, Bud, or Flower.",
        "Lock Yellow Pikmin maturity to Default, Leaf, Bud, or Flower.",
        "Lock Blue Pikmin maturity to Default, Leaf, Bud, or Flower.",
        "Lock White Pikmin maturity to Default, Leaf, Bud, or Flower.",
        "Lock Purple Pikmin maturity to Default, Leaf, Bud, or Flower.",
        "Set starting Pikmin in Slot 1 (lead) on match start and respawn.",
        "Set starting Pikmin in Slot 2 on match start and respawn.",
        "Set starting Pikmin in Slot 3 on match start and respawn.",
        "Set starting Pikmin in Slot 4 on match start and respawn.",
        "Lineup preset spawned by pressing D-Pad Up in Presets mode.",
        "Lineup preset spawned by pressing D-Pad Down in Presets mode.",
        "Lineup preset spawned by pressing D-Pad Left in Presets mode.",
        "Lineup preset spawned by pressing D-Pad Right in Presets mode.",
    ],
    "kRailDesc": [
        "Frame advance, Player 1 damage, speed, camera, and controls.",
        "One-click P+ practice presets and Combo Trainer settings.",
        "CPU damage, DI, SDI, tech habits, counters, and recovery AI.",
        "Record, edit, loop, mirror, and export multi-slot inputs.",
        "Hitboxes, heatmaps, DI vectors, overlays, and info panels.",
        "Toggle on-screen frame data and technique notifications.",
        "Fighter swap, active stage options, and character mechanics.",
        "Press A twice to reset all Training Lab settings to defaults.",
        DESC_TRAINING_MENU,
        DESC_EXIT_CSS,
    ],
}

# Indexed by ui::pages::PageId, plus P_COUNT for Category Rail entries.
PAGE_TABLES = [
    ("kGeneralDesc", "P_GENERAL"),
    ("kScenariosDesc", "P_SCENARIOS"),
    ("kCpuDesc", "P_CPU"),
    ("kRecordingDesc", "P_RECORDING"),
    ("kDisplayDesc", "P_DISPLAY"),
    ("kOsdDesc", "P_OSD_MENU"),
    ("kStageCastDesc", "P_STAGE_CAST"),
    ("kControlsDesc", "P_CONTROLS"),
    ("kDebugDesc", "P_DEBUG"),
    ("kComboTrainerDesc", "P_COMBO_TRAINER"),
    ("kComboMetersDesc", "P_COMBO_METERS"),
    ("kTechDesc", "P_TECH"),
    ("kAdvCounterDesc", "P_ADVANCED_COUNTER"),
    ("kSlotsDesc", "P_SLOTS"),
    ("kAlterDesc", "P_ALTER_INPUTS"),
    ("kSlotChancesDesc", "P_HMN_CHANCES"),
    ("kSlotChancesDesc", "P_CPU_CHANCES"),
    ("kOverlaysDesc", "P_HMN_OVERLAYS"),
    ("kOverlaysDesc", "P_CPU_OVERLAYS"),
    ("kInfoDesc", "P_HMN_INFO"),
    ("kInfoDesc", "P_CPU_INFO"),
    ("kLedgedashDesc", "P_LEDGEDASH_HUD"),
    ("kFighterSwapDesc", "P_FIGHTER_SWAP"),
    ("kThemeDesc", "P_THEME"),
    ("kReversalDesc", "P_REVERSALS"),
    ("kCpuDiDesc", "P_CPU_DI"),
    ("kCpuCountersDesc", "P_CPU_COUNTERS"),
    ("kCpuDefenseDesc", "P_CPU_DEFENSE"),
    ("kCounterPoolDesc", "P_COUNTER_POOL"),
    ("kOlimarDesc", "P_OLIMAR"),
    ("kOsdIndividualDesc", "P_OSD_INDIVIDUAL"),
    ("kTechWeightsDesc", "P_TECH_WEIGHTS"),
    ("kRecManageDesc", "P_REC_MANAGE"),
    ("kCollisionDesc", "P_COLLISION"),
    ("kHudOverlaysDesc", "P_HUD_OVERLAYS"),
    ("kRailDesc", "P_COUNT (Category Rail entries)"),
]

def _dist_slot(dist):
    if dist <= 4:
        return dist - 1, 0, 0
    v = dist - 1
    extra = v.bit_length() - 2
    slot = (extra + 1) * 2 + ((v >> extra) & 1)
    base = ((2 + (slot & 1)) << extra) + 1
    return slot, dist - base, extra


def _huff_lengths(freqs, n_syms, max_bits=15):
    import heapq
    lens = [0] * n_syms
    active = [(w, (s,)) for s, w in enumerate(freqs) if w > 0]
    if not active:
        return lens
    if len(active) == 1:
        lens[active[0][1][0]] = 1
        return lens
    heapq.heapify(active)
    while len(active) > 1:
        w1, s1 = heapq.heappop(active)
        w2, s2 = heapq.heappop(active)
        for s in s1 + s2:
            lens[s] += 1
        heapq.heappush(active, (w1 + w2, s1 + s2))
    # Cap any depth > max_bits while preserving Kraft sum <= 1.
    while max(lens) > max_bits:
        for i in range(n_syms):
            if lens[i] > max_bits:
                lens[i] = max_bits
        kraft = sum((1 << (max_bits - l)) for l in lens if l > 0)
        limit = 1 << max_bits
        while kraft > limit:
            # Lengthen the longest non-max code (lowest weight first).
            cands = [(freqs[i], -lens[i], i) for i in range(n_syms) if 0 < lens[i] < max_bits]
            cands.sort()
            _, _, idx = cands[0]
            kraft -= 1 << (max_bits - lens[idx] - 1)
            lens[idx] += 1
    return lens


def _canonical_codes(lens):
    codes = {}
    code = 0
    for bits in range(1, 16):
        for s, l in enumerate(lens):
            if l == bits:
                codes[s] = (code, bits)
                code += 1
        code <<= 1
    return codes


def encode_huff_lzss(raw):
    from collections import defaultdict
    for b in raw:
        if b >= 0x80:
            raise ValueError(f"Non-ASCII byte {b:#x} in menu descriptions")
    n = len(raw)
    trigrams = defaultdict(list)
    matches = [[] for _ in range(n)]
    for i in range(n - 2):
        tg = raw[i:i + 3]
        lst = trigrams[tg]
        best = {}
        limit = min(34, n - i)
        for p in reversed(lst):
            dist = i - p
            if dist > 32768:
                break
            l = 3
            while l < limit and raw[p + l] == raw[i + l]:
                l += 1
            for ll in range(3, l + 1):
                if ll not in best:
                    best[ll] = dist
            if l == limit:
                break
        if best:
            matches[i] = sorted(best.items())
        lst.append(i)

    # Iterative optimal DP parsing under exact canonical Huffman bit costs.
    ll_cost = [7] * 128 + [6] * 32
    d_cost = [5] * 30
    tokens = []
    for _pass in range(3):
        dp = [10**9] * (n + 1)
        choice = [None] * n
        dp[n] = 0
        for i in range(n - 1, -1, -1):
            c_lit = ll_cost[raw[i]] + dp[i + 1]
            dp[i] = c_lit
            choice[i] = (0, raw[i], 0)
            for ll, dist in matches[i]:
                sl, _, eb = _dist_slot(dist)
                c_m = ll_cost[125 + ll] + d_cost[sl] + eb + dp[i + ll]
                if c_m < dp[i]:
                    dp[i] = c_m
                    choice[i] = (1, ll, dist)
        tokens = []
        ll_freq = [0] * 160
        d_freq = [0] * 30
        i = 0
        while i < n:
            kind, a, b = choice[i]
            if kind == 0:
                tokens.append((0, a, 0))
                ll_freq[a] += 1
                i += 1
            else:
                tokens.append((1, a, b))
                ll_freq[125 + a] += 1
                sl, _, _ = _dist_slot(b)
                d_freq[sl] += 1
                i += a
        ll_lens = _huff_lengths(ll_freq, 160)
        d_lens = _huff_lengths(d_freq, 30)
        ll_cost = [l if l > 0 else 15 for l in ll_lens]
        d_cost = [l if l > 0 else 15 for l in d_lens]

    ll_codes = _canonical_codes(ll_lens)
    d_codes = _canonical_codes(d_lens)

    out = bytearray()
    for i in range(0, 160, 2):
        out.append((ll_lens[i] << 4) | ll_lens[i + 1])
    for i in range(0, 30, 2):
        out.append((d_lens[i] << 4) | d_lens[i + 1])

    acc = 0
    nbits = 0

    def put_bits(val, bits):
        nonlocal acc, nbits
        for k in range(bits - 1, -1, -1):
            acc = (acc << 1) | ((val >> k) & 1)
            nbits += 1
            if nbits == 8:
                out.append(acc)
                acc = 0
                nbits = 0

    for kind, a, b in tokens:
        if kind == 0:
            code, bits = ll_codes[a]
            put_bits(code, bits)
        else:
            code, bits = ll_codes[125 + a]
            put_bits(code, bits)
            sl, ex, eb = _dist_slot(b)
            dc, db = d_codes[sl]
            put_bits(dc, db)
            if eb:
                put_bits(ex, eb)
    if nbits:
        out.append(acc << (8 - nbits))
    return bytes(out)


def decode_huff_lzss(comp, raw_len):
    def build(nibbles, n_syms):
        lens = []
        for b in nibbles:
            lens.append(b >> 4)
            lens.append(b & 0xF)
        count = [0] * 16
        for l in lens[:n_syms]:
            if l:
                count[l] += 1
        first_code = [0] * 16
        first_sym = [0] * 16
        code = 0
        sym_off = 0
        for b in range(1, 16):
            first_code[b] = code
            first_sym[b] = sym_off
            code = (code + count[b]) << 1
            sym_off += count[b]
        syms = [0] * sym_off
        offs = list(first_sym)
        for s, l in enumerate(lens[:n_syms]):
            if l:
                syms[offs[l]] = s
                offs[l] += 1
        return count, first_code, first_sym, syms

    ll_tab = build(comp[0:80], 160)
    d_tab = build(comp[80:95], 30)
    bit_pos = [95 * 8]

    def read_bit():
        p = bit_pos[0]
        bit_pos[0] = p + 1
        return (comp[p >> 3] >> (7 - (p & 7))) & 1

    def dec(tab):
        count, first_code, first_sym, syms = tab
        code = 0
        for b in range(1, 16):
            code = (code << 1) | read_bit()
            d = code - first_code[b]
            if d < count[b]:
                return syms[first_sym[b] + d]
        raise AssertionError("Bad Huffman code")

    out = bytearray()
    while len(out) < raw_len:
        sym = dec(ll_tab)
        if sym < 128:
            out.append(sym)
        else:
            ln = sym - 125
            sl = dec(d_tab)
            if sl < 4:
                dist = sl + 1
            else:
                eb = (sl - 2) >> 1
                ex = 0
                for _ in range(eb):
                    ex = (ex << 1) | read_bit()
                dist = ((2 + (sl & 1)) << eb) + 1 + ex
            for _ in range(ln):
                out.append(out[-dist])
    return bytes(out)


def format_u8_array(values, indent="        ", per_line=16):
    rows = []
    for i in range(0, len(values), per_line):
        chunk = values[i:i + per_line]
        rows.append(indent + ", ".join(f"0x{v:02X}" for v in chunk) + ",")
    if rows:
        rows[-1] = rows[-1].rstrip(",")
    return "\n".join(rows)


def generate_cpp():
    uniq_tables = []
    for t, _ in PAGE_TABLES:
        if t not in uniq_tables:
            uniq_tables.append(t)

    row_strings = []
    table_span = {}
    for t in uniq_tables:
        start = len(row_strings)
        for s in TABLES[t]:
            for ch in s:
                b = ord(ch)
                if b < 0x20 or b >= 0x7F:
                    raise ValueError(f"Non-printable ASCII byte {b:#x} in {s!r}")
            row_strings.append(s)
        table_span[t] = (start, len(TABLES[t]))

    raw = b"".join(s.encode("ascii") + b"\x00" for s in row_strings)
    if len(raw) > 65535:
        raise ValueError(f"Raw stream {len(raw)} exceeds u16 offset range")
    comp = encode_huff_lzss(raw)
    if decode_huff_lzss(comp, len(raw)) != raw:
        raise AssertionError("Huffman+LZSS round-trip mismatch in menu descriptions")

    page_rows = []
    for idx, (t, cmt) in enumerate(PAGE_TABLES):
        start, count = table_span[t]
        comma = "," if idx + 1 < len(PAGE_TABLES) else " "
        page_rows.append(f"        {{{start:3d}, {count:2d}}}{comma} // {cmt}")

    cpp = f"""// Generated by scripts/gen_menu_descriptions.py - edit descriptions there.
#include "extra/menu_descriptions.h"

#include "extra/lz77_ascii.h"
#ifdef __MWERKS__
#include "extra/net_alloc.h"
#endif
#include "main/ui/pages/pages.h"

namespace tm {{
namespace extra {{
    using namespace ui::pages;

    struct DescSpan {{
        u16 start;
        u8 count;
    }};

    enum {{
        kRowCount = {len(row_strings)},
        kRawBytes = {len(raw)},
        kCompBytes = {len(comp)}
    }};

    struct MenuDescNet {{
        char text[kRawBytes];
        u16 offsets[kRowCount];
    }};

    // Canonical Huffman + LZSS packed NUL-terminated description strings ({len(comp)} B vs {len(raw)} B raw).
    static const u8 kDescComp[kCompBytes] = {{
{format_u8_array(comp)}
    }};

    // Indexed by ui::pages::PageId, plus P_COUNT for Category Rail entries.
    static const DescSpan kTables[P_COUNT + 1] = {{
{chr(10).join(page_rows)}
    }};

#ifdef __MWERKS__
    static MenuDescNet* s_net = NULL;
#else
    static MenuDescNet s_hostNet;
    static MenuDescNet* s_net = NULL;
#endif

    static bool populate(MenuDescNet* net)
    {{
        if (net == NULL)
        {{
            return false;
        }}
        return decompressHuffLzss(kDescComp, net->text, kRawBytes)
               && indexPackedStrings(net->text, kRawBytes, net->offsets, kRowCount);
    }}

    void menuDescAttach()
    {{
#ifdef __MWERKS__
        freeNetPtr(s_net);
        s_net = allocNetArray<MenuDescNet>(1);
        if (!populate(s_net))
        {{
            freeNetPtr(s_net);
        }}
#else
        if (s_net == NULL && populate(&s_hostNet))
        {{
            s_net = &s_hostNet;
        }}
#endif
    }}

    void menuDescDetach()
    {{
#ifdef __MWERKS__
        freeNetPtr(s_net);
#else
        s_net = NULL;
#endif
    }}

    const char* optionDesc(u8 pageId, u8 row)
    {{
#ifndef __MWERKS__
        if (s_net == NULL)
        {{
            menuDescAttach();
        }}
#endif
        if (s_net == NULL || pageId > P_COUNT)
        {{
            return NULL;
        }}
        const DescSpan& t = kTables[pageId];
        if (row >= t.count)
        {{
            return NULL;
        }}
        return s_net->text + s_net->offsets[t.start + row];
    }}
}}
}}
"""
    return cpp


def main():
    root = pathlib.Path(__file__).resolve().parent.parent
    out_path = root / "src" / "extra" / "menu_descriptions.cpp"
    cpp = generate_cpp()
    if "--check" in sys.argv[1:]:
        current = out_path.read_bytes().decode("ascii")
        if current != cpp:
            sys.stderr.write("error: src/extra/menu_descriptions.cpp is out of date; run scripts/gen_menu_descriptions.py\n")
            return 1
        print("menu_descriptions: up to date")
        return 0
    out_path.write_bytes(cpp.encode("ascii"))
    print(f"wrote {out_path} ({len(cpp)} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
