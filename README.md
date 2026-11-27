# Training Mode for Project+

Training Mode for Project+ is a Project+-first training lab plugin that expands
Brawl's Training mode with a native P+/Brawl menu system, one-click practice
scenarios, savestates, multi-slot input recording and playback, programmable CPU
behavior, 44-fighter PPlusAI recovery, Combo Trainer, frame advance, hitbox
heatmaps, grab/tether visualization, dual-stick DI prediction, fighter info
panels, color overlays, live stage options, mid-session fighter swaps,
per-fighter mechanics controls, and on-screen technique displays. It also folds
in ProjectPunch's MIT-licensed training tools so both do not need to be
installed side by side.

## Installing

**Into your own Project+ build: one file.** The whole mod is a single plugin
file, `TrainingMode.rel`. Copy it into `Project+/pf/plugins/` on your Project+
SD card (or virtual `sd.raw` image). Nothing else in the build is changed, and
deleting the file removes the mod. See [docs/install.md](docs/install.md) for
step-by-step Dolphin and Wii instructions.

This plugin cannot be installed alongside ProjectPunch; Project+ does not have
the memory for both.

## Requirements

- Project+ v3.2.1
- Dolphin (primary tested environment; earlier pre-releases have booted and
  saved on real Wii hardware, while newer additions are still Dolphin-tested
  first)
- Your own Super Smash Bros. Brawl (USA, RSBE01 rev 2) disc image
- Offline play only; do not use in netplay

## Using it

When you enter Training's character select, the **Training Menu** opens over the
select screen (hold X or Y on a GameCube controller while booting if you want
Versus instead). Use **L / R** (`- / +` on Wii Remote / Nunchuk) to switch
between the 6 sections (**All Modes**, **Offense**, **Defense**, **Tech
Practice**, **Challenges**, **Character Trainers**), launch **Training Lab**,
any of the 9 preconfigured **Scenario Presets**, any of the 7 universal **Tech
Practice** drills, **Challenges** (including **Break the Targets!**), or
**Character Trainers** (**Snake Recovery**, **Float / Hover & DJC**). Press
**X** to open the **Recordings** browser for exported `.tmr` files, press **Y**
(`Z` on Nunchuk, `- + A` on Sideways) to open **Quick Overlays** before
entering a match, or press **B** (`1` on Sideways) to return cleanly to Brawl's
Solo menu.

On Training's character select, a mode pill shows your armed mode and quick
reopen shortcut. Press **B** (when P1 has no placed coin to recall and no
dropdown is open) or **L+R** (Classic: L+R, Sideways Wii Remote: `- + B`,
Nunchuk: C+Z) to step back into the Training Menu.

GameCube controllers and Wii controllers (Sideways Wii Remote, Wii Remote +
Nunchuk, and Classic Controller) work across the Training Menu, Quick Overlays,
Recordings browser, and in-match lab controls (opening and navigating the lab
menu, frame advance, savestates, and D-pad shortcuts).

Once in a Training match, press **Start** (`+` on Wii controllers) to open the
lab menu, and press it again to close. Switch between the **Category Rail** and
**Top Tabs** layouts live under **Controls > Menu Style**. Press **X** to reset
a single highlighted option to its default, or double-tap **Y** to reset the
current page cleanly. To keep Start for Brawl's Training pause menu, set
**Controls > Menu Open** to **Z in Pause**, then open the lab menu from Brawl's
pause menu (Z on GC/Nunchuk, ZL/ZR on Classic, `-` on Sideways).

Project+'s in-match Code Menu shortcut (`L+R+D-pad Down`) is disabled by default
in Training so it does not collide with lab shortcuts. Enable **Controls > P+
Code Menu Shortcut** to allow it in Training Lab. Main Menu access (**Versus >
Codes**) and regular Versus matches are untouched.

## Features

- **Native P+/Brawl v1 UI (CSS Training Menu, Quick Overlays & In-Match Lab Menu):**
  - 6-section CSS launcher (**All Modes**, **Offense**, **Defense**, **Tech
    Practice**, **Challenges**, **Character Trainers**) with **Training Lab**,
    9 practice scenarios, 7 universal tech drills, 7 **Challenges** (including
    **Break the Targets!**), 2 **Character Trainers**, the **Recordings**
    browser (`X`), and **Quick Overlays** (`Y`), plus stage/CPU recommendation
    badges and a CRT-safe CSS mode pill.
  - Switchable **Category Rail** and **Top Tabs** in-match layouts across 7
    categories (**Lab**, **Scenarios**, **CPU**, **Recording**, **Display**,
    **OSDs**, **Stage & Cast**) with v1 pop-up modals, per-option reset (`X`),
    2-tap per-page reset (`Y`), customizable menu accents, single-line option
    descriptions, native controller button emblems, and 4:3 + 16:9 CRT-safe
    layout.
- **Tech Practice, Challenges & Character Trainers:**
  - **Tech Practice (7 single-page drills):** **L-Cancel**, **Wavedash /
    Waveland**, **Ledgedash**, **Powershield**, **RAR**, **DACUS**, and
    **B-Reverse / Wavebounce**.
  - **Challenges:** **Cancel Practice: Endurance**, **Movement Flow Course**,
    **Situational Reaction Lab**, **Universal Techchase Escape**, **Universal
    Recovery Sweetspot**, **CC / ASDI-Down**, and **Break the Targets!**.
  - **Character Trainers:** **Snake Infinite Recovery** and **Float / Hover & DJC**.
  - Defaults to infinite **Free Practice** with live hit rate, streak, best
    streak (persisted to SD), and mode-specific frame/angle/GALINT readouts.
    Press **L + D-pad Up** in-match to switch between **Free Practice**,
    **10-Rep Set**, and **20-Rep Set** (with a set summary modal on 10/20-rep
    completion).
- **Scenarios & Combo Trainer:**
  - 9 unified Project+ practice setups running directly on the lab engine:
    **Combo Lab** (inline knobs and clean grounded defaults), **Techchase**,
    **Edgeguard** (with ledge-trap continuity), **Shield Pressure / OoS**
    (switch offense/defense role in the Scenarios menu), **Shield Drop**,
    **Ledgetech** (with live spacing control), **Slide-Off** (including
    Knockdown mode), **Grab Defense / Throw DI**, and **P+ Multi-Hit SDI**
    (with automatic resets, side flip, right-margin live stat plates, and
    temporary preset overrides that never overwrite your saved SD settings).
  - **Combo Trainer**: hitstun-drop auto-reset with optional `noclip` warps,
    fixed or random reset position and damage, reset timer offset, offstage
    recovery looping, and 7 named 3D floating state meters (Hitlag, Hitstun,
    Shieldstun, DI, IASA, Ledge Intangibility, Intangibility).
- **Savestates:**
  - Hold D-pad Right to save, press D-pad Left to load.
  - Works mid-attack, in hitlag, during grabs, on ledges, and with projectiles
    or partners out (two players at most; Ice Climbers count as one).
  - Supports adding random extra percent on load for P1 and CPU.
- **Recording & Playback:**
  - Record and play back inputs for both P1 and CPU across six slots each,
    backed by a savestate for deterministic replay.
  - **Start On Input** arming, weighted random slot selection, mirrored
    playback, loop playback, auto restore, start paused, and live HMN/CPU
    takeover or re-recording.
  - **Slot Management > Modify Inputs**: edit individual frames (stick,
    C-stick, analog triggers, and fighter buttons).
  - **Frame Rewind**: step back frame by frame while Frame Advance holds a
    playback.
  - Export recordings to `/Project+/tm/` on the SD card and reload them from
    the Training Menu.
- **CPU Control, Custom Counter Pools, Recovery & Adaptive AI:**
  - Behaviors: Stand, Shield, Crouch, Jump, Powershield, and Brawl Training AI
    fallback, plus configurable **SHFFL / FFL** aerial pressure options.
  - **44-Fighter PPlusAI Recovery**: pure C++ offstage recovery across all 8
    Project+ recovery families (arc, glide, float, tether, charge, PK Thunder
    orbit, and more).
  - **Custom Counter Pools & Reversals**: counter actions on hit, on shield, in
    the air, and **On-Ledge** (`Cliff_Wait`) with configurable frame delay,
    mix-and-match custom pools (**Nothing**, full P+ ground/shield/air moves,
    **SHFFL/FFL**, **DJC/Float**, and recorded **Slot 1..6**), weighted
    multi-slot **Reversal Slots**, and 10-hit **Per-Hit Counters** tables.
  - Trajectory DI (10 modes including **Custom** live 2D C-stick/stick picker
    and **Just Out**), Smash DI, **Dual-Stick SDI**, and ASDI modeled on
    Project+'s rules.
  - Tech and get-up options (including banana trips / `Slip_Wait`) with
    independent weights, tech trap simulation, tech lockout timing, invisible
    techs, tech sound cue, **Clatter Escape** (mash out of buries, sleep, and
    freeze), and **Adaptive CPU** habit tracking.
  - Infinite shields with configurable shield health (up to 60) and shield
    angle, grab escape/release, Move CPU, Freeze CPU, and controller handoff.
- **General, Controls & 4-Player Support:**
  - Frame advance on L/Z/X/Y/R (hold past configurable delay to run at speed),
    player and CPU percent set/lock, player infinite shields, game speed
    (`1`, `5/6`, `2/3`, `1/2`, `1/4`), camera modes (`Normal`, `Zoom`, `Fixed`),
    move staling toggle, and full **4P Training** plus **4P Offline VS**
    read-only displays (when Assist Trophies are OFF).
  - Configurable D-pad bindings, Frame Rewind button, Lab Menu open button,
    Menu Style toggle, and P+ Code Menu shortcut toggle.
- **Display, Frame Meter, Overlays & Visualization:**
  - **Display** and **Display > Lab Debug** split: Project+'s native
    hitboxes/hurtboxes (persisting during frame advance) with configurable
    **Hitbox Color Modes**, stage collision and ECBs, plus toggles for Match
    HUD, Fighter Models, Stage Model, Effects, **PSA Debugger** (read-only
    11-thread & FSM Inspector), and Debug Overlay under **Lab Debug**.
  - **Frame Meter**: aspect-adaptive SF6-style dual-strip live frame timeline
    showing startup, active, recovery, stun, and advantage frame-by-frame.
  - **Input Viewer**: live multi-controller input display supporting GameCube,
    Classic Controller, Wii Remote + Nunchuk, and Sideways Wii Remote layouts.
  - **Hitbox Heatmap & Reach Bounds**: persistent multi-frame attack coverage
    with frame-age color bands, opacity control, and world-space reach bounds.
  - **Grab / Tether / Item-Pickup Visualizer**: live 3D grabboxes, search/tether
    zones, and item pickup areas for P1, CPU, or Both.
  - **DI Display**: real-time launch trajectory preview during hitlag with
    Project+ TDI, ASDI, SDI, Dual-Stick C-stick macro preview, stage collision
    marks, and missed-tech slide or wall/ceiling bounce prediction.
  - **Color Overlays**: per-state fighter tinting, hurtbox toggle, invisible
    toggle, or sound cue for P1 and CPU (including the ProjectPunch preset).
  - **Info Display**: up to 8 configurable rows per fighter across 3 sizes,
    reading 34 live Brawl/P+ values including PSA **Actionable In**, **Can
    Walljump**, and **Wall Cling**.
  - **Ledgedash HUD**: per-frame phase bar, GALINT/ledge/angle summary adapted
    from ProjectPunch, and P+ 5-ledge-grab intangibility limit tracking.
- **On-Screen Displays, Presets & Timing Helpers:**
  - 20 universal Smash/P+ OSDs (Wavedash, L-Cancel with P+ Auto L-Cancel rule
    awareness, Act OoS, Dashback, Powershield, SDI Inputs, Tech Lockout, Item
    Throw / Glide Toss, Boost Grab / DACUS, Act OoLag, Act OoAirborne, Jump
    Cancel, Fastfall, Shield and Hit Frame Advantage with PSA script lookahead,
    Combo Counter, Grab Escape, Ledgedash Info with 5-grab limit warning, Act
    OoHitstun, and P+ Tech Tracker).
  - One-click **OSD Presets** (`Essentials`, `Movement`, `Defense`, `All Off`,
    `Custom`), **Overhead Timing Bar** (L-Cancel, Wavedash, Act-Out), **Tech
    Sparkle** visual cues (Perfect Wavedash / Shorthop), configurable screen
    position (`HUD`, `Sides`, `Top Left`, `Top Right`), advantage tolerance,
    and max visible messages.
- **Stage & Cast & Olimar Lab Kit:**
  - **Mid-Session Fighter Swap**: swap P1 and CPU fighters and costumes (with
    live stock-icon previews) without leaving the match.
  - **Stage Options**: live hazards toggle, moving platform freeze/presets, and
    Fountain of Dreams independent platform heights on supported stages.
  - **Olimar Lab Kit (`Stage & Cast -> Olimar >`)**: dedicated submenu with
    live B+D-Pad Pikmin Pluck, 16 competitive Lineup Presets on D-pad, 5-color
    Pull Pool toggles, Whistle / Lead targeting + Move Lead Now, per-color and
    global Maturity locks, and 4-slot Starting Lineup customization.
  - **Fighter RNG & Mechanics (18 fighters)**: Peach Turnip/F-Smash/Float,
    Luigi Misfire, Mr. Game & Watch Judge/Oil Panic, Samus Charge/Beam/Air
    Bombs, Mewtwo Shadow Ball/Hover, Lucario Aura/Sphere, Wario Waft, DK Giant
    Punch, Ivysaur Solarbeam, R.O.B. Air Boosts/Gyro/Beam, Lucas Offense Up,
    Squirtle Water Gun, Sheik Needles, King Dedede Waddle Toss, Snake
    Tranq/Cypher, Ganondorf Float, Ice Climbers Nana Throw, and Olimar.
- **SD Card Persistence & Safe Upgrades:**
  - Lab settings, Quick Overlays edits, and Tech Practice best streaks save
    automatically to `/Project+/tm/` and reload on the next boot. When loading
    an older `v1` settings file, a Settings Update dialog lets you choose
    **Reset All Settings (Recommended)**, **Reset Affected Settings**, or
    **Keep My Settings** before writing a clean `v2` file. See
    [docs/files.md](docs/files.md).

See [docs/limitations.md](docs/limitations.md) for current limitations and what
has been tested.

## Development

See [docs/building.md](docs/building.md) to build the mod and
[docs/testing.md](docs/testing.md) for the host and Dolphin test suites.

## Credits

This mod stands on a lot of community work. [CREDITS.md](CREDITS.md) explains
each source and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) holds the
file-by-file record and licences. Key projects and contributors:

- **fudgepop01**, for [PPlusAI](https://github.com/fudgepop01/PPlusAI) and
  SuperTraining (44-fighter recovery, Combo Trainer, Hitbox Heatmap, Adaptive
  CPU, and frame-data structures), adapted with express permission.
- **Wisp**, for [ProjectPunch](https://github.com/WispSSBM/ProjectPunch) (MIT
  license), and **ilikepizza107** for the Partially Invisible Techs design.
- **lil klon**, for his ProjectPunch dummy tools (Start On Input, On-Ledge
  reversals) and P+ 3.2.1 notes, used with permission.
- [Project+](https://projectplusgame.com) by the Project+ Development Team, and
  code authors including **Eon** (hitbox/hurtbox and stage collision debug
  drawing, L-cancel, airdodge, and Training codes) and **QuickLava**
  ([PowerPC-Assembly-Functions](https://github.com/QuickLava/PowerPC-Assembly-Functions)).
- **Sammi Husky**, for [Syriinge](https://github.com/Sammi-Husky/Syriinge),
  [SyriingePluginTemplate](https://github.com/Sammi-Husky/SyriingePluginTemplate)
  (MIT), and [BrawlHeaders](https://github.com/Sammi-Husky/BrawlHeaders).
- The [Brawl decompilation](https://github.com/doldecomp/brawl) (CC0) and
  [BrawlRE](https://github.com/BrawlRE/BrawlRE.github.io) (MIT).
- [Brawl Online](https://brawlonline.net) (with sarahkittyy's OK) and
  [Brawlback](https://github.com/Brawlback-Team).

## License

Copyright (c) 2026 skier. The mod's own code is under the GNU General Public License v3.0 or
later (`GPL-3.0-or-later`, [LICENSE](LICENSE)). Assets, text and routines copied, adapted or
ported from other projects keep their own terms; [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)
lists them file by file with their licences.

This project isn't affiliated with any of the above, or with Nintendo. The repository
contains no game data. You need your own copy of the game and your own Project+ build.
