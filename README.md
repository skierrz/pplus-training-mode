<div align="center">

<img src="docs/assets/logo.png" alt="Project+ Training Mode Mod (P+TMM) Logo" width="160" />

# Project+ Training Mode Mod (P+TMM)

A free, open-source training lab plugin for **Project+ v3.2.1** that expands Brawl's Training mode with savestates, input recording, programmable CPUs, practice drills, frame data, and visualizers.

[![Download Releases](https://img.shields.io/badge/Releases-Download-16A34A?style=for-the-badge&logo=github&logoColor=white)](https://github.com/skierrz/pplus-training-mode/releases)
[![Discord](https://img.shields.io/badge/Discord-Join%20Server-5865F2?style=for-the-badge&logo=discord&logoColor=white)](https://discord.gg/WHNtJd2KAJ)
[![Project+ v3.2.1](https://img.shields.io/badge/Project%2B-v3.2.1-00B4A6?style=for-the-badge)](https://projectplusgame.com)
[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-3B82F6?style=for-the-badge)](LICENSE)

</div>

> [!IMPORTANT]
> **Discord Server:** Join the **[P+TMM Discord](https://discord.gg/WHNtJd2KAJ)** for test builds, bug reports, setup help, and shared `.tmr` recordings.

> [!NOTE]
> **AI Use and Credits:** P+TMM is developed with AI assistance and builds on years of foundational Project+ and Brawl modding work. Every feature is backed by Project+ 3.2.1 research, Wii GX UI design, automated Dolphin and Wii testing, and hands-on playtesting. [Read the full disclosure and credits](#ai-use-and-credits).

---

## Installation

### Option 1: Prebuilt Dolphin / Packed SD (Recommended for Dolphin)

1. Download the prebuilt Dolphin package (or pre-packed `sd.raw`) from [Releases](https://github.com/skierrz/pplus-training-mode/releases) or the [P+TMM Discord](https://discord.gg/WHNtJd2KAJ).
2. Extract it, point Dolphin at your Super Smash Bros. Brawl (USA, `RSBE01` rev 2) ISO, and launch Project+.

### Option 2: Add to an Existing Project+ v3.2.1 Build (Dolphin or Wii)

Download `TrainingMode.rel` from [Releases](https://github.com/skierrz/pplus-training-mode/releases) and copy it into `Project+/pf/plugins/` on your SD card.

- **Dolphin (`sd.raw`):** Open **Config -> Wii -> SD Card Settings**, enable **Insert SD Card** and **Allow Writes to SD Card**, click **Unpack SD Card Now**, copy `TrainingMode.rel` into `Project+/pf/plugins/`, then click **Pack SD Card Now**.
- **Wii:** Copy `TrainingMode.rel` directly into `Project+/pf/plugins/` on your SD card.

Delete `TrainingMode.rel` to uninstall. No other files in your Project+ build are modified. See [docs/install.md](docs/install.md) for the full setup walkthrough.

> **Note on ProjectPunch:** Project+ does not have enough memory to load P+TMM alongside [ProjectPunch](https://github.com/WispSSBM/ProjectPunch), so keep only one plugin in `Project+/pf/plugins/` at a time. P+TMM builds on Wisp's MIT-licensed ProjectPunch work (with Wisp's blessing) and carries over its core training tools (frame advance, ledgedash/GALINT HUD, color overlays, invisible techs, and frame advantage) so you keep those features when switching.

### Requirements

- **Project+ v3.2.1**
- **Super Smash Bros. Brawl** (USA, `RSBE01` rev 2) disc image
- **Dolphin** or **Wii** (Dolphin is the primary test target; Wii compatibility is actively maintained, but real Wii hardware is not yet thoroughly tested)
- **Offline play only** (do not use on netplay)

---

## Controls

GameCube, Classic Controller, Wii Remote + Nunchuk, and Sideways Wii Remote are all supported.

### Character Select (Boot Menu)

| Action | GameCube | Wii / Classic |
|---|---|---|
| Switch tabs (`Quick Start`, `Scenarios`, `Tech Practice`, `Minigames & Tech`) | `L` / `R` | `-` / `+` |
| Open Quick Setup (overlays and OSD presets) | `Y` | `Z` (Nunchuk) / `- + A` (Sideways) |
| Open Recordings Browser (`.tmr` files on SD) | `X` | `X` / `1` |
| Close menu to CSS / Hold to exit to Solo menu | Tap `B` / Hold `B` | Tap / Hold `B` (`1` Sideways) |
| Reopen Boot Menu from CSS | `L + R` | `L + R` / `C + Z` / `- + B` |

### In-Match

| Action | GameCube | Wii / Classic |
|---|---|---|
| Open / Close Lab Menu | `Start` | `+` |
| Save / Load Savestate | Hold `D-Pad Right` / Tap `D-Pad Left` | Same |
| Switch Drill Mode (`Free Practice` / `10-Rep` / `20-Rep`) | `L + D-Pad Up` | Same |
| Reset highlighted option / Reset current page (in Lab Menu) | `X` / Double-tap `Y` | Same |

- **Keep `Start` for Brawl's pause menu:** Set **Lab > Controls > Menu Open** to **Z in Pause**.
- **P+ Code Menu shortcut (`L + R + D-Pad Down`):** Disabled by default inside Training to avoid shortcut conflicts. Re-enable it under **Lab > Controls > P+ Code Menu Shortcut**. Versus mode is unchanged.

---

## Features

### Practice Modes and Drills
Launch **Training Lab** for an open sandbox, or pick a ready setup from the 4-tab Boot Menu:
- **Scenarios:** One-click setups for Combo Lab, Techchase, Edgeguard, Shield Pressure / OoS, Shield Drop, Ledgetech, Slide-Off, Grab Defense / Throw DI, Multi-Hit SDI, and CC / ASDI-Down. Scenario settings are temporary and never overwrite your saved SD config.
- **Tech Practice:** Guided drills tracking live hit rate, streaks, SD-saved best streaks, and timing/angle/GALINT readouts (L-Cancel, Wavedash / Waveland, Ledgedash, Powershield, RAR, DACUS, B-Reverse / Wavebounce).
- **Minigames & Character Trainers:** Break the Targets!, Float / Hover & DJC, and Snake C4 Recovery.

### Savestates and Input Recording
- **Savestates:** Save and load cleanly mid-attack, in hitlag, during grabs, on ledge, and with projectiles or Ice Climbers active. Supports optional random percent variance on load.
- **6-Slot Recording (P1 and CPU):** Savestate-backed deterministic playback with **Start On Input**, weighted random slot playback, mirrored playback, looping, live takeover/re-record, per-frame input editing, Frame Rewind, and `.tmr` SD export/import (`/Project+/tm/`).

### Programmable CPU and Combo Trainer
- **44-Fighter P+ AI Recovery:** Pure C++ offstage recovery covering all P+ recovery families.
- **Counter Pools & Reversals:** Trigger counters on hit, on shield, in the air, or on ledge with custom delays, move pools, weighted Reversal Slots, 10-hit Per-Hit Counter tables, and Adaptive CPU habit tracking.
- **DI, Techs & Combo Trainer:** P+ DI (including live 2D Custom stick aiming and Just Out), SDI, Dual-Stick SDI, ASDI, weighted tech/getup mixups, invisible techs, bury/stun mashing, and hitstun-drop Combo Trainer auto-resets.

### Frame Data, Visualizers, and OSDs
- **Live Frame Meter:** Dual-row timeline displaying startup, active strikes, active grabs, vulnerable lag, stun, intangibility/GALINT, and frame advantage, color-synced with the Ledgedash HUD and Floating Overhead Meters.
- **Visualizers:** Native P+ hitboxes/hurtboxes (with Property and Priority color modes), Hitbox Heatmap with reach bounds, 3D Grab/Tether/Item-Pickup boxes, real-time DI trajectory preview, multi-controller Input Viewer, Color Overlays, 34-field Info Display, and a read-only PSA Debugger.
- **20 On-Screen Displays:** Built-in presets (`Essentials`, `Movement`, `Defense`, `All Off`) plus 3 saveable **Custom 1-3** slots.
- **Colorblind Mode:** Built-in **Display > Theme > Colorblind Mode** (`Off`, `Deuteran`, `Protan`, `Tritan`) across OSDs, Frame Meter, Ledgedash HUD, and Floating Meters.

<details>
<summary>Full list of 20 On-Screen Displays</summary>

- Wavedash / Waveland timing and angle
- L-Cancel (with P+ ALC rule awareness)
- Ledgedash / GALINT (with 5-ledgegrab limit tracking)
- Powershield (physical, reflect, and Light->Hard)
- Boost Grab and DACUS
- Frame Advantage on Shield and on Hit (with PSA lookahead)
- Act OoS, Act OoWait, Act OoAutocancel, and Act OoHitstun
- Jump Cancel, Fastfall, and Dashback
- Combo Counter, SDI Inputs, Tech Lockout, Mash Out, Item Throw / Glide Toss, and P+ Tech Tracker

</details>

### Stage, Cast, and Character Tools
- **Mid-Match Swaps & Stage Control:** Switch fighters and costumes without leaving the match, toggle stage hazards, and control moving platforms (including Fountain of Dreams heights).
- **Olimar Lab Kit & 18-Fighter RNG Controls:** Live B+D-Pad Pikmin pluck, 16 lineup presets, pull pools, maturity locks, Whistle/Lead tools, and dedicated RNG/resource controls for 18 fighters.
- **4-Player & Offline VS Displays:** Full 4-player Training support plus optional read-only displays in Offline VS (with Assist Trophies off).

<details>
<summary>Supported character RNG and mechanics controls (18 fighters)</summary>

Peach (Turnip, F-Smash, Float), Luigi (Misfire), Mr. Game & Watch (Judge, Oil Panic), Samus (Charge Shot, Beam, Air Bombs), Mewtwo (Shadow Ball, Hover), Lucario (Aura, Aura Sphere), Wario (Waft), Donkey Kong (Giant Punch), Ivysaur (Solarbeam), R.O.B. (Air Boosts, Gyro, Laser), Lucas (Offense Up), Squirtle (Water Gun), Sheik (Needles), King Dedede (Waddle Toss), Snake (Tranq, Cypher), Ganondorf (Float), Ice Climbers (Nana Throw), and Olimar (full Pikmin kit).

</details>

Settings and best streaks save automatically to `/Project+/tm/` on your SD card.

---

## Documentation

| Guide | Covers |
|---|---|
| [docs/install.md](docs/install.md) | Dolphin, `sd.raw`, and Wii installation |
| [docs/files.md](docs/files.md) | SD card `/Project+/tm/` file layout and save upgrades |
| [docs/limitations.md](docs/limitations.md) | Current limitations and tested hardware |
| [docs/building.md](docs/building.md) | Compiling from source (`Normal`, `PERF=1`, `STAGE4C=0`) |
| [docs/testing.md](docs/testing.md) | Host unit tests and automated Dolphin suites |
| [docs/internals.md](docs/internals.md) & [docs/internals-extra.md](docs/internals-extra.md) | Plugin architecture, hooks, and memory layout |

### Quick Build

Requires Metrowerks CodeWarrior 4.2 for Wii, devkitPro MSYS2, and Python 3.10.

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

- **Engineering and architecture:** The plugin is split across multiple Wii `.rel` modules to fit within Brawl's memory limits, keeps SD save formats backwards-compatible, and compiles with zero warnings across all build configurations (`Normal`, `PERF=1`, `STAGE4C=0`). Changes are verified with host unit tests, automated Dolphin gameplay suites, and strict Wii-compatibility checks (`MMU`, `LLE`, and heap fragmentation).
- **Project+ 3.2.1 accuracy:** Frame windows, fighter states, memory addresses, and physics rules are verified against Project+ 3.2.1 source, Brawl disassembly, and live Dolphin runtime behavior rather than assumed from Melee.
- **Game and systems design:** Scenarios, drills, recording tools, and CPU behaviors are structured around practical Project+ training workflows, including session-only scenario presets that do not overwrite saved SD settings, deterministic savestate-backed playback, and weighted counter pools.
- **UI and UX design:** Menus and overlays are built natively for Wii GX hardware and 480i CRTs in both `16:9` and `4:3`, using standard Smash/P+ terminology, a unified action-state color system with colorblind modes, and controller-tested navigation.

### Credits

P+TMM builds on years of reverse engineering, tooling, and training-mod groundwork by the Project+ and Brawl modding community. See [CREDITS.md](CREDITS.md) for full details and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for the file-by-file record and licenses.

- **Wisp** - [ProjectPunch](https://github.com/WispSSBM/ProjectPunch) (MIT), the pioneering Project+ training plugin whose frame advance, ledgedash/GALINT tracking, frame advantage, overlays, and fighter-state tools P+TMM builds directly on (with Wisp's blessing), alongside **ilikepizza107** for Partially Invisible Techs.
- **fudgepop01** - [PPlusAI](https://github.com/fudgepop01/PPlusAI) and SuperTraining, whose 44-fighter recovery framework, Combo Trainer, Hitbox Heatmap, Adaptive CPU, and PSA Inspector specification underpin major parts of P+TMM (adapted with express permission).
- **lil klon** - ProjectPunch dummy tools (Start On Input, On-Ledge reversals, reversal slots) and Project+ 3.2.1 research notes, used with permission.
- **Project+ Development Team & PMDT** - [Project+](https://projectplusgame.com), including **Eon**, **QuickLava** ([PowerPC-Assembly-Functions](https://github.com/QuickLava/PowerPC-Assembly-Functions)), **Magus**, **DukeItOut**, **Fracture17** ([ProjectMCodes](https://github.com/Fracture17/ProjectMCodes) symbol/code references), and **Kapedani**.
- **Sammi Husky** - [Syriinge](https://github.com/Sammi-Husky/Syriinge), [SyriingePluginTemplate](https://github.com/Sammi-Husky/SyriingePluginTemplate) (MIT), and [BrawlHeaders](https://github.com/Sammi-Husky/BrawlHeaders), which make C++ plugins inside Brawl possible.
- **[doldecomp/brawl](https://github.com/doldecomp/brawl)** (CC0) and **[BrawlRE](https://github.com/BrawlRE/BrawlRE.github.io)** (MIT) for reverse engineering and documenting Brawl's internals.
- **[Brawl Online](https://brawlonline.net)** (with sarahkittyy's permission) and the **[Brawlback](https://github.com/Brawlback-Team)** team for rollback memory ranges, Dolphin tooling, and loader architecture research.

---

## License

Copyright (c) 2026 skier. Original code in this repository is licensed under **GPL-3.0-or-later** ([LICENSE](LICENSE)). Adapted and third-party components retain their respective licenses in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

This project is not affiliated with Project+, Brawl Online, Brawlback, or Nintendo. No game assets are included; you must provide your own copy of Super Smash Bros. Brawl and Project+.
