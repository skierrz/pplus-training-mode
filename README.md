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
Versus instead). Use **L / R** to switch sections (**All Modes**, **Offense**,
**Defense**), launch **Training Lab** or any of the 10 preconfigured **Scenario
Presets**, press **X** to open the **Recordings** browser for exported `.tmr`
files, or press **B** to close the menu. Press **L+R** (Classic: L+R, Sideways
Wii Remote: `- + B`, Nunchuk: C+Z) to bring it back.

GameCube controllers and Wii controllers (Sideways Wii Remote, Wii Remote +
Nunchuk, and Classic Controller) work across both the Training Menu /
Recordings browser and in-match lab controls (opening and navigating the lab
menu, frame advance, savestates, and D-pad shortcuts).

Once in a Training match, press **Start** (`+` on Wii controllers) to open the
lab menu, and press it again to close. Switch between the **Category Rail** and
**Top Tabs** layouts live under **Controls > Menu Style**. To keep Start for
Brawl's Training pause menu, set **Controls > Menu Open** to **Z in Pause**,
then open the lab menu from Brawl's pause menu (Z on GC/Nunchuk, ZL/ZR on
Classic, `-` on Sideways).

Project+'s in-match Code Menu shortcut (`L+R+D-pad Down`) is disabled by default
in Training so it does not collide with lab shortcuts. Enable **Controls > P+
Code Menu Shortcut** to allow it in Training Lab. Main Menu access (**Versus >
Codes**) and regular Versus matches are untouched.

## Features

- **Native P+/Brawl UI (CSS Training Menu & In-Match Lab Menu):**
  - 3-section CSS launcher (**All Modes**, **Offense**, **Defense**) with
    **Training Lab**, 10 practice presets, and the **Recordings** browser.
  - Switchable **Category Rail** and **Top Tabs** in-match layouts across 7
    categories (**Lab**, **Scenarios**, **CPU**, **Recording**, **Display**,
    **OSDs**, **Stage & Cast**) with single-line option descriptions, live
    controller button glyphs, and 4:3 + 16:9 CRT-safe layout.
- **Scenarios & Combo Trainer:**
  - 10 unified Project+ practice setups running directly on the lab engine:
    **Combo Lab**, **Techchase**, **Edgeguard & Recovery**, **Shield
    Pressure**, **OoS / Reversal**, **Shield Drop**, **Ledgetech**,
    **Slide-Off**, **Grab Escape**, and **Multi-Hit SDI** (with automatic
    resets and side flip; temporary preset overrides never overwrite your saved
    SD settings).
  - **Combo Trainer**: hitstun-drop auto-reset with `noclip` warps, fixed or
    random reset position and damage, reset timer offset, offstage recovery
    looping, and 7 named 3D floating state meters (Hitlag, Hitstun, Shieldstun,
    DI, IASA, Ledge Intangibility, Intangibility).
- **Savestates:**
  - Hold D-pad Right to save, press D-pad Left to load.
  - Works mid-attack, in hitlag, during grabs, on ledges, and with projectiles
    or partners out (two players at most; Ice Climbers count as one).
  - Supports adding random extra percent on load.
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
- **CPU Control, Recovery & Adaptive AI:**
  - Behaviors: Stand, Shield, Crouch, Jump, Powershield, and Brawl Training AI
    fallback.
  - **44-Fighter PPlusAI Recovery**: pure C++ offstage recovery across all 8
    Project+ recovery families (arc, glide, float, tether, charge, PK Thunder
    orbit, and more).
  - Counter actions on hit, on shield, in the air, and **On-Ledge** (`Cliff_Wait`)
    with configurable frame delay, full P+ action lists, random pools,
    playback-slot counters, and 10-hit **Per-Hit Counters** tables.
  - Trajectory DI (`Random`, `In`, `Out`, `Survival`, `None`), Smash DI,
    **Dual-Stick SDI**, and ASDI modeled on Project+'s rules.
  - Tech and get-up options (including banana trips / `Slip_Wait`) with
    independent weights, tech trap simulation, tech lockout timing, invisible
    techs, tech sound cue, **Clatter Escape** (mash out of buries, sleep, and
    freeze), and **Adaptive CPU** habit tracking.
  - Infinite shields with configurable shield health (up to 60) and shield
    angle, grab escape/release, Move CPU, Freeze CPU, and controller handoff.
- **General & Controls:**
  - Frame advance on L/Z/X/Y/R (hold past configurable delay to run at speed),
    player and CPU percent set/lock, player infinite shields, game speed
    (`1`, `5/6`, `2/3`, `1/2`, `1/4`), camera modes (`Normal`, `Zoom`, `Fixed`),
    and move staling toggle.
  - Configurable D-pad bindings, Frame Rewind button, Lab Menu open button,
    Menu Style toggle, and P+ Code Menu shortcut toggle.
- **Display, Overlays & Visualization:**
  - Project+'s native hitboxes/hurtboxes (persisting during frame advance),
    stage collision and ECBs, and toggles for Match HUD, Fighter Models, Stage
    Model, Effects, and Debug Overlay.
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
- **On-Screen Displays (19 universal Smash/P+ OSDs):**
  - Wavedash, L-Cancel, Act OoS, Dashback, Powershield, SDI Inputs, Tech
    Lockout, Item Throw / Glide Toss, Boost Grab / DACUS, Act OoLag, Act
    OoAirborne, Jump Cancel, Fastfall, Frame Advantage (shield and hit, with
    PSA script lookahead on shield), Combo Counter, Grab Escape, Ledgedash
    Info (with 5-grab limit warning), and Act OoHitstun.
  - Configurable screen position (`HUD`, `Sides`, `Top Left`, `Top Right`),
    advantage tolerance, and max visible messages.
- **Stage & Cast:**
  - **Mid-Session Fighter Swap**: swap P1 and CPU fighters and costumes (with
    live stock-icon previews) without leaving the match.
  - **Stage Options**: live hazards toggle, moving platform freeze/presets, and
    Fountain of Dreams independent platform heights on supported stages.
  - **Fighter RNG & Mechanics (17 fighters)**: Peach Turnip/F-Smash/Float, Luigi
    Misfire, Mr. Game & Watch Judge/Oil Panic, Samus Charge/Beam/Air Bombs,
    Mewtwo Shadow Ball/Hover, Lucario Aura/Sphere, Wario Waft, DK Giant Punch,
    Ivysaur Solarbeam, R.O.B. Air Boosts/Gyro/Beam, Lucas Offense Up, Squirtle
    Water Gun, Sheik Needles, King Dedede Waddle Toss, Snake Tranq/Cypher,
    Ganondorf Float, and Ice Climbers Nana Throw.
- **SD Card Persistence:**
  - Lab settings save automatically to `/Project+/tm/` when closing the lab
    menu and reload on the next boot. See [docs/files.md](docs/files.md).

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
