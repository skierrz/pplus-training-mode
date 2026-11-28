<div align="center">

<img src="docs/assets/logo.png" alt="Project+ Training Mode Mod (P+TMM) Logo" width="160" />

# Project+ Training Mode Mod (P+TMM)

A free, open-source training lab plugin for **Project+ v3.2.1** that expands Brawl's Training mode with savestates, input recording, programmable CPUs, practice scenarios, visualizers, and on-screen displays.

[![Download Releases](https://img.shields.io/badge/Releases-Download-16A34A?style=for-the-badge&logo=github&logoColor=white)](https://github.com/skierrz/pplus-training-mode/releases)
[![Discord](https://img.shields.io/badge/Discord-Join%20Server-5865F2?style=for-the-badge&logo=discord&logoColor=white)](https://discord.gg/WHNtJd2KAJ)
[![Project+ v3.2.1](https://img.shields.io/badge/Project%2B-v3.2.1-00B4A6?style=for-the-badge)](https://projectplusgame.com)
[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-3B82F6?style=for-the-badge)](LICENSE)

</div>

> [!IMPORTANT]
> **Discord Server:** Join the **[P+TMM Discord](https://discord.gg/WHNtJd2KAJ)** for newer development builds, bug reports, setup help, and shared `.tmr` recordings.

> [!NOTE]
> **AI Use and Credits:** P+TMM is developed with AI assistance and builds on years of foundational Project+ and Brawl modding work. Every feature is backed by Project+ 3.2.1 research, Wii GX UI design, automated Dolphin testing, and hands-on playtesting. [Read the full disclosure and credits](#ai-use-and-credits).

---

## Installation

### Option 1: Pre-Packed SD Card Image (Recommended for Dolphin)

1. Download `TrainingMode-1.0.1-pre3-Dolphin-sd.zip` from [Releases](https://github.com/skierrz/pplus-training-mode/releases) (or grab the latest test build in the [P+TMM Discord](https://discord.gg/WHNtJd2KAJ)).
2. Extract `sd.raw` to a separate folder.
3. In Dolphin (**Config -> Wii -> SD Card Settings**), point **SD Card Path** at that `sd.raw`, enable **Insert SD Card** and **Allow Writes to SD Card**, and uncheck **Automatically Sync with Folder**.
4. Launch **Project+ Offline Launcher** with your Super Smash Bros. Brawl (USA, `RSBE01` rev 2) ISO.

### Option 2: Add to an Existing Project+ v3.2.1 Build (Dolphin or Wii)

Download `TrainingMode.rel` (or `TrainingMode-1.0.1-pre3-SD-card-files.zip`) from [Releases](https://github.com/skierrz/pplus-training-mode/releases) and copy `TrainingMode.rel` into `Project+/pf/plugins/` on your SD card.

- **Dolphin (`sd.raw`):** Open **Config -> Wii -> SD Card Settings**, enable **Insert SD Card** and **Allow Writes to SD Card**, click **Unpack SD Card Now**, copy `TrainingMode.rel` into `Project+/pf/plugins/`, then click **Pack SD Card Now**.
- **Wii:** Copy `TrainingMode.rel` directly into `Project+/pf/plugins/` on your SD card.

Delete `TrainingMode.rel` to uninstall. No other files in your Project+ build are modified. See [docs/install.md](docs/install.md) for the full setup walkthrough.

> **Note on ProjectPunch:** Project+ does not have enough memory to load P+TMM alongside [ProjectPunch](https://github.com/WispSSBM/ProjectPunch), so keep only one plugin in `Project+/pf/plugins/` at a time. P+TMM builds on Wisp's MIT-licensed ProjectPunch work (with Wisp's blessing) and carries over its core training tools (frame advance, ledgedash/GALINT HUD, color overlays, invisible techs, and frame advantage) so you keep those features when switching.

### Requirements

- **Project+ v3.2.1**
- **Super Smash Bros. Brawl** (USA, `RSBE01` rev 2) disc image
- **Dolphin** or **Wii** (Dolphin is the primary test target; Wii compatibility is actively maintained, though real Wii hardware is not yet thoroughly tested)
- **Offline play only** (do not use on netplay)

---

## Controls (`v1.0.1-pre3`)

GameCube, Classic Controller, Wii Remote + Nunchuk, and Sideways Wii Remote are all supported.

### Character Select (Training Menu)

| Action | GameCube | Classic | Nunchuk | Sideways Wii Remote |
|---|---|---|---|---|
| Boot to Versus instead of Training | Hold `X` or `Y` | - | - | - |
| Switch sections (`All Modes`, `Offense`, `Defense`) | `L` / `R` | `L` / `R` (`ZL` / `ZR`) | `-` / `+` | `-` / `+` |
| Open Recordings Browser (`.tmr` files) | `X` | `X` | `C` | `A` |
| Close menu to CSS | `B` | `B` | `B` | `1` |
| Reopen Training Menu from CSS | `L + R` | `L + R` | `C + Z` | `- + B` |

### In-Match

| Action | GameCube | Classic | Nunchuk | Sideways Wii Remote |
|---|---|---|---|---|
| Open / Close Lab Menu | `Start` | `+` | `+` | `+` |
| Open Brawl's Training menu (from Lab Menu) | `Z` | `ZL` / `ZR` | `Z` | `-` |
| Save Savestate | Hold `D-Pad Right` | Hold `D-Pad Right` | Hold `D-Pad Right` | Hold `-` + `D-Pad Right` |
| Load Savestate | Tap `D-Pad Left` | Tap `D-Pad Left` | Tap `D-Pad Left` | Hold `-` + `D-Pad Left` |

- Switch between Category Rail (`Rail (C)`) and Top Tabs (`Tabs (A)`) live under **Controls > Menu Style**.
- To keep `Start` for Brawl's pause menu, set **Controls > Menu Open** to **Z in Pause**.
- Project+'s in-match Code Menu shortcut (`L + R + D-Pad Down`) is disabled by default inside Training to avoid shortcut conflicts. Re-enable it under **Controls > P+ Code Menu Shortcut**. Versus mode is unchanged.

---

## Features (`v1.0.1-pre3`)

*(Newer features including Tech Practice drills, the Live Frame Meter, Quick Setup, and Colorblind Mode are in active development on the `dev` branch and in Discord test builds.)*

### Training Menu, Scenarios, and Combo Trainer
- **Native P+ UI:** 3-section CSS Training Menu (`All Modes`, `Offense`, `Defense`) and switchable Category Rail / Top Tabs in-match Lab Menu with Brawl fonts, controller glyphs, stock icons, and `16:9` / `4:3` CRT-safe layout.
- **10 Scenario Presets:** One-click setups running on the lab engine (`Combo Lab`, `Techchase`, `Edgeguard`, `Shield Pressure`, `OoS / Reversal`, `Shield Drop`, `Ledgetech`, `Slide-Off`, `Grab Escape`, and `P+ Multi-Hit SDI`), with automatic resets, side flip, and session-only overrides that leave your saved SD settings untouched.
- **Combo Trainer:** Hitstun-drop auto-reset with `noclip` warps, fixed or random reset position and damage, timer adjustment, offstage recovery looping, and 7 floating world-space state meters (Hitlag, Hitstun, Shieldstun, DI, IASA, Ledge Intangibility, Intangibility).

### Savestates and Input Recording
- **Savestates:** Save and restore cleanly mid-attack, in hitlag, during grabs, on ledge, and with projectiles or Ice Climbers active (requires 2 players / 1 CPU; Ice Climbers count as 1). Supports optional random percent variance on load.
- **6-Slot Recording (P1 and CPU):** Savestate-backed deterministic playback with **Start On Input**, weighted random slot selection, mirrored playback, looping, auto-restore, live takeover/re-record, per-frame input editing, Frame Rewind, and `.tmr` SD export/import (`/Project+/tm/`).

### Programmable CPU and P+ AI
- **44-Fighter PPlusAI Recovery:** Pure C++ offstage recovery covering all 8 Project+ recovery families.
- **Counters and Reversals:** Trigger counters on hit, on shield, in the air, or on ledge with configurable frame delays, `Random`, `Random Custom`, 10-hit `Per-Hit Counters`, and recorded `Slot 1-6` playback, plus Adaptive CPU habit tracking.
- **DI, Techs, and Defense:** Project+ Trajectory DI (`Random`, `In`, `Out`, `Survival`, `None`), Smash DI, Dual-Stick SDI, ASDI, weighted tech/getup mixups (including banana trips), invisible techs, tech trap simulation, Clatter Escape (bury/sleep/freeze mashing), and configurable infinite shields.

### Visualizers and On-Screen Displays
- **Visualizers:** Native P+ hitboxes/hurtboxes (persisting during frame advance), stage collision and ECBs, Hitbox Heatmap with reach bounds, 3D Grab/Tether/Item-Pickup boxes, real-time Dual-Stick DI trajectory preview with collision and bounce prediction, Color Overlays (including the ProjectPunch preset), Ledgedash HUD (with 5-ledgegrab limit tracking), and a 34-metric Info Display (`Actionable In`, `Can Walljump`, `Wall Cling`, and more).
- **19 On-Screen Displays:** Configurable screen position (`HUD`, `Sides`, `Top Left`, `Top Right`), advantage tolerance, and max visible messages.

<details>
<summary>Full list of 19 On-Screen Displays in v1.0.1-pre3</summary>

- Wavedash
- L-Cancel
- Ledgedash Info (with 5-ledgegrab limit warning)
- Powershield
- Boost Grab / DACUS
- Frame Advantage (on shield with PSA lookahead, and on hit)
- Act OoS, Act OoLag, Act OoAirborne, and Act OoHitstun
- Jump Cancel, Fastfall, and Dashback
- Combo Counter, SDI Inputs, Tech Lockout, Grab Escape, and Item Throw / Glide Toss

</details>

### Stage, Cast, and Character Controls
- **Mid-Match Fighter & Costume Swap** with stock-icon previews without leaving Training.
- **Stage Options:** Live hazards toggle, moving platform freeze/presets, and Fountain of Dreams independent platform heights.
- **17-Fighter RNG & Mechanics Controls** covering key character resources and random pulls.

<details>
<summary>Supported character RNG and mechanics controls (17 fighters)</summary>

Peach (Turnip, F-Smash, Float), Luigi (Misfire), Mr. Game & Watch (Judge, Oil Panic), Samus (Charge Shot, Beam, Air Bombs), Mewtwo (Shadow Ball, Hover), Lucario (Aura, Aura Sphere), Wario (Waft), Donkey Kong (Giant Punch), Ivysaur (Solarbeam), R.O.B. (Air Boosts, Gyro, Laser), Lucas (Offense Up), Squirtle (Water Gun), Sheik (Needles), King Dedede (Waddle Toss), Snake (Tranq, Cypher), Ganondorf (Float), and Ice Climbers (Nana Throw).

</details>

Settings save automatically to `/Project+/tm/` on your SD card.

---

## Documentation

| Guide | Covers |
|---|---|
| [docs/install.md](docs/install.md) | Dolphin, `sd.raw`, and Wii installation |
| [docs/files.md](docs/files.md) | SD card `/Project+/tm/` file layout |
| [docs/limitations.md](docs/limitations.md) | Current limitations and tested hardware |
| [docs/building.md](docs/building.md) | Compiling from source (toolchain, targets, and module budgets) |
| [docs/testing.md](docs/testing.md) | Host unit tests, `PERF=1` profiling, and automated Dolphin suites |
| [docs/internals.md](docs/internals.md) & [docs/internals-extra.md](docs/internals-extra.md) | Plugin architecture, hooks, and memory layout |

### Quick Build

Requires Metrowerks CodeWarrior 4.2 for Wii, devkitPro MSYS2, and Python 3.8 or newer.

```bash
make tools       # download CodeWarrior and elf2rel
make -j4         # build build/TrainingMode.rel
make check-host  # run host unit and layout tests
```

---

## Community and Bug Reports

- **Discord:** [Join the P+TMM Discord](https://discord.gg/WHNtJd2KAJ) for test builds, discussion, and support.
- **Bug Reports:** Open a [GitHub Issue](https://github.com/skierrz/pplus-training-mode/issues) with the fighter, stage, settings, and steps to reproduce.

---

## AI Use and Credits

### Development and AI Use

P+TMM is developed with AI coding assistance under continuous human direction, review, and playtesting. AI tools are used to accelerate implementation and testing, while the architecture, game mechanics, and user experience follow standard software engineering and Project+ modding practices:

- **Engineering and architecture:** The plugin is split across multiple Wii `.rel` modules to fit within Brawl's memory limits, keeps SD save formats cleanly structured, and compiles with zero warnings across build configurations (`Normal` and `PERF=1`). Changes are verified with host unit tests and automated Dolphin gameplay suites.
- **Project+ 3.2.1 accuracy:** Frame windows, fighter states, memory addresses, and physics rules are verified against Project+ 3.2.1 source, Brawl disassembly, and live Dolphin runtime behavior rather than assumed from Melee.
- **Game and systems design:** Scenarios, recording tools, and CPU behaviors are structured around practical Project+ training workflows, including session-only scenario presets that do not overwrite saved SD settings, deterministic savestate-backed playback, and multi-trigger counters.
- **UI and UX design:** Menus and overlays are built natively for Wii GX hardware and 480i CRTs in both `16:9` and `4:3`, with clear navigation and full GameCube and Wii controller support.

### Credits

P+TMM builds on years of reverse engineering, tooling, and training-mod groundwork by the Project+ and Brawl modding community. See [CREDITS.md](CREDITS.md) for full details and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for the file-by-file record and licenses.

- **Wisp** - [ProjectPunch](https://github.com/WispSSBM/ProjectPunch) (MIT), the pioneering Project+ training plugin whose frame advance, ledgedash/GALINT tracking, frame advantage, overlays, and fighter-state tools P+TMM builds directly on (with Wisp's blessing), alongside **ilikepizza107** for Partially Invisible Techs.
- **fudgepop01** - [PPlusAI](https://github.com/fudgepop01/PPlusAI) and SuperTraining, whose 44-fighter recovery framework, Combo Trainer, Hitbox Heatmap, and Adaptive CPU underpin major parts of P+TMM (adapted with express permission).
- **lil klon** - ProjectPunch dummy tools (Start On Input and On-Ledge reversals) and Project+ 3.2.1 research notes, used with permission.
- **Project+ Development Team & PMDT** - [Project+](https://projectplusgame.com), including **Eon**, **QuickLava** ([PowerPC-Assembly-Functions](https://github.com/QuickLava/PowerPC-Assembly-Functions)), **Magus**, **DukeItOut**, **Fracture17** ([ProjectMCodes](https://github.com/Fracture17/ProjectMCodes) symbol/code references), and **Kapedani**.
- **Sammi Husky** - [Syriinge](https://github.com/Sammi-Husky/Syriinge), [SyriingePluginTemplate](https://github.com/Sammi-Husky/SyriingePluginTemplate) (MIT), and [BrawlHeaders](https://github.com/Sammi-Husky/BrawlHeaders), which make C++ plugins inside Brawl possible.
- **[doldecomp/brawl](https://github.com/doldecomp/brawl)** (CC0) and **[BrawlRE](https://github.com/BrawlRE/BrawlRE.github.io)** (MIT) for reverse engineering and documenting Brawl's internals.
- **[Brawl Online](https://brawlonline.net)** (with sarahkittyy's permission) and the **[Brawlback](https://github.com/Brawlback-Team)** team for rollback memory ranges, Dolphin tooling, and loader architecture research.

---

## License

Copyright (c) 2026 skier. Original code in this repository is licensed under **GPL-3.0-or-later** ([LICENSE](LICENSE)). Adapted and third-party components retain their respective licenses in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

This project is not affiliated with Project+, Brawl Online, Brawlback, or Nintendo. No game assets are included; you must provide your own copy of Super Smash Bros. Brawl and Project+.
