# Building

The plugin is built with the Metrowerks CodeWarrior 4.2 compiler (`mwcceppc`,
`mwldeppc`) and `elf2rel`, the same toolchain Syriinge and its plugin template
use, plus Python 3.8 or newer (standard library only) to pack the modules
into one file. The output is `build/TrainingMode.rel`: a small stub plugin
(module ID 21581) with the three modules it loads appended to it (see "The
stub and its modules" below).

These steps are for Windows. The toolchain is a set of Windows executables.

## Requirements

- [devkitPro](https://devkitpro.org/wiki/Getting_Started) with its bundled
  MSYS2. Only its `make` and basic shell utilities are used; devkitPPC itself
  is not needed and `DEVKITPPC` does not have to be set.
- Python 3.8 or newer (standard library only) for `scripts/pack-rel.py` and
  `scripts/rel-exports.py`.
- git (devkitPro's MSYS2 ships one; Git for Windows also works).
- Network access to github.com for the first setup.

Clone into a path without spaces; make cannot handle them.

## Steps

Open the MSYS2 shell that comes with devkitPro (`msys2/msys2.exe` inside the
devkitPro install folder), then:

```sh
git clone --recurse-submodules https://github.com/skierrz/pplus-training-mode.git pplus-training-mode
cd pplus-training-mode
make tools
make
```

If you cloned without `--recurse-submodules`, run
`git submodule update --init --recursive` first.

`make tools` runs `scripts/fetch-tools.sh`, which fetches the compiler and
`elf2rel` from
[SyriingePluginTemplate](https://github.com/Sammi-Husky/SyriingePluginTemplate)
at a pinned commit into `tools/` (git-ignored) and checks every file against a
recorded SHA-256. To copy them from an existing template checkout instead of
downloading, run `make tools TOOLS_SRC=path/to/SyriingePluginTemplate`.

### Using another shell

Any shell works as long as devkitPro's `msys2/usr/bin` directory is on `PATH`
ahead of other `make`/`sh` installs, so that `make`, `sh`, `sed`, `awk` and `tr`
come from it. For example, from PowerShell:

```powershell
$env:PATH = "<devkitPro>\msys2\usr\bin;" + $env:PATH
make
```

or as a one-off from any prompt:

```
<devkitPro>\msys2\usr\bin\bash.exe -lc "cd /c/path/to/pplus-training-mode && make"
```

where `<devkitPro>` is your devkitPro install folder.

## Targets

| Command      | Effect                                                   |
|--------------|----------------------------------------------------------|
| `make`       | build `build/TrainingMode.rel` (incremental)             |
| `make V=1`   | same, printing full compiler and linker command lines    |
| `make clean` | delete `build/`                                          |
| `make tools` | populate `tools/`; does nothing if already up to date    |
| `make negative` | build the deliberately broken plugin files `make test-reject` uses, and the padded one `make test-big-file` uses |
| `make check-host` | build part of the mod with the PC's own C++ compiler (Visual Studio's, if installed) into `build/host/` and run its logic tests on the PC; it does not use, and is not used by, the build of the plugin |

The emulator targets and `make check-host` are described in
[testing.md](testing.md).

All intermediates (`.o`, dependency files, `.elf`, `.map`, each module's
`.rel`) and the packed `.rel` go in `build/` (`build/stub/`, `build/main/`,
`build/menu/`, `build/extra/`).

`make` prints the sizes and fails if the stub `.rel` is larger than
`STUB_BUDGET` (20 KiB), the main module needs more than `MAIN_BUDGET`
bytes when loaded (the block the stub allocates for it, see "Load" below):
the largest size that passed the stress tests in its heap, the menu module more
than `MENU_BUDGET` (96 KiB), the extra module more than `EXTRA_BUDGET`
(418,816 bytes / 409 KiB, leaving 608 B below `AssistFigureResource`'s
419,424 B usable MEM2 cap), or the peak unpacked module file size exceeds
`TEMP_BUDGET` (495,616 bytes / 484 KiB, guarding `scMelee`'s `MenuInstance`
temporary link heap). All can be overridden on the command line for
experiments.

## Local settings

Copy `local.mk.example` to `local.mk` (git-ignored) to override settings such
as the tools location. Never commit machine-specific paths.

## Installing

Copy `build/TrainingMode.rel` into `/Project+/pf/plugins/` on the Project+
SD card (or virtual SD card), where Syriinge loads plugins at boot. Keep the
file name: the stub reads its own file again by that name (Syriinge does not
tell a plugin its file name). Loading has been verified in Dolphin with
Project+ 3.2.1; see [testing.md](testing.md) for `make install`, `make run`
and the tests, which do this against a project-only SD folder.

Expected debug log (OSReport) lines at boot, in this order: ours are printed
from `_prolog`, which Syriinge calls before logging the plugin as loaded.

```
[TrainingMode] v1.0.1 loaded
...
[TrainingMode] modules: /Project+/pf/plugins/TrainingMode.rel: ok; ...
[TrainingMode] module MTCH: id 21582, ...: ok
[Syringe] Loaded plugin (TrainingMode, v1.0.1)
```

## The stub and its modules

The Syriinge heap on Project+ 3.2.1 is 64 KiB, shared with sy_core and the
other plugins, so only a small stub lives there. Everything else is in
modules the stub loads for the scenes that need them and unloads afterwards:

| Module | Kind, id | Loaded for | Heap | What it holds |
|---|---|---|---|---|
| main (`src/main/`) | "match", 21582 | every Training match | OverlayMenu | the lab, its menu, displays, CPU options, savestates, recording, the SD card |
| extra (`src/extra/`) | "extra", 21584 | every Training match and opted-in offline Versus (when Assist Trophies are off), after the main module | AssistFigureResource (MEM2) | DI Display, PSA frame data, Action Log, Character RNG & Fighter Mechanics, Stage Options, PPlusAI recovery, Combo Trainer, Hitbox Heatmap, Grab/Tether Visualizer, Adaptive CPU, Scenario Presets, Fighter Swap, native UI assets, menu descriptions, and pure skill drill engines |
| menu (`src/menu/`) | "menu", 21583 | Training's character select | OverlayMenu | the Training Menu launcher and the recordings import browser |

- **File.** `scripts/pack-rel.py` writes the stub REL, then each module REL
  (32-byte aligned), then a table (per module: id, kind tag such as `MTCH`,
  offset, size, FNV-1a checksum) and a 32-byte trailer pointing at it. The
  format is described in the script and in `src/stub/packed_file.h`.
  Syriinge links only the stub: `gfModule::create` copies the REL's first
  `fixSize` bytes and never looks past them, so the appended modules cost the
  Syriinge heap nothing.
- **Boot.** The stub installs every hook (none is ever installed by a
  module), reads its own file once (`plugins/TrainingMode.rel`, the relative
  form Syriinge uses, which Project+'s file patch resolves under its SD folder
  `MOD_PATCH_DIR`), checks the table and every module's header and checksum,
  and logs the verdict per module.
- **Load.** A policy (`src/stub/load_policy.cpp`) maps the scene that is about
  to start to the modules to load and their heaps: for a Training match
  (`scMelee`), the main module into OverlayMenu, then the extra module into
  AssistFigureResource (MEM2, below); for opted-in local offline Versus, the
  extra module also uses AssistFigureResource when Assist Trophies are off (2P
  to 4P), falling back to OverlayFighter4 for 2P/3P (or match-only in 4P) when
  Assist Trophies are on; for Training's character select
  (`scSelctCharacter` while the sequence is `sqTraining`), the menu module
  into OverlayMenu: that scene allocates hundreds of blocks in MenuInstance,
  while OverlayMenu is untouched there. Module by module, the loader reads
  each module's byte range of the file into a temporary buffer in
  MenuInstance (a ranged SD read: no buffer for the whole file, so the
  file's size does not matter; the match layout's MenuInstance has 0x79A60
  bytes free, enough for one module at a time), checks the image again,
  copies the first `fixSize` bytes into a permanent block in the target heap
  and links it the way the game links its own modules (`gfModule::create`):
  with `OSLinkFixed`, `.bss` at `fixSize` rounded up to 32, and the
  relocation tables read from the temporary buffer. `OSLinkFixed` applies all
  the relocations, keeps only the import entries for other modules
  (`sora_melee`, and the main module for the extra module) and clears `.bss`
  last; the loader then repoints the kept import table into the permanent
  block, flushes the instruction and data caches and frees the temporary
  buffer before the module runs. The permanent block is therefore only
  `fixSize` plus `.bss` (each rounded up to 32); that is the "bytes loaded"
  size `make` prints and checks against the budgets, alongside a separate
  line for the peak temporary buffer size. `scripts/pack-rel.py` fails the
  build if a module's imports from the game or from itself do not come last,
  or if anything kept lies past `fixSize`. Then the loader calls the module's
  `_prolog`, checks the interface version and kind it reports, and calls its
  `attach`, which fills the callback slots of the interface
  (`src/shared/interface.h`): `render` (after each frame is drawn), `pads`
  and `fighterPad` (the controllers, see "Hooks"). The stub also hands the
  module a 768-byte settings block it keeps for the whole session, so the
  lab's settings survive the module being unloaded between matches; the module
  checks the block's header (magic, layout version, size) and resets an old
  layout. The first match of a session reads the settings from the SD card
  into it (see [files.md](files.md)); the stub itself never touches the card
  after reading its own file. The block also carries the SD card state of the
  session and an import from the menu module to the match module
  (`src/main/storage/block.h`).
- **Unload.** Just before the scene's `exit()`, the stub nulls the callback
  slots, and for each module, the last loaded first, calls `detach`,
  `_epilog` (destructors), `OSUnlink`, frees the block and logs whether the
  heap is back to where it was before the load.

## The extra module

The extra module holds the heavier training services so the main module stays
within its OverlayMenu budget: DI Display (`src/extra/di_display.*`), PSA frame
data (`src/extra/frame_data.*`: when a fighter can act again, read ahead from
its move's scripts for `Actionable In`, post-dodge CPU gating, and shield
advantage lookahead), Alter Inputs (`src/extra/lab_tools.*`), Character RNG and
Fighter Mechanics (`src/extra/character_rng.*`, `src/extra/fighter_mechanics.*`),
Stage Options (`src/extra/stage_options.*`, `src/extra/stage_geom.*`), 44-fighter
PPlusAI recovery (`src/extra/pplus_recovery*`), Combo Trainer
(`src/extra/combo_trainer*`), Hitbox Heatmap (`src/extra/hitbox_heatmap*`),
Grab / Tether Visualizer (`src/extra/grab_visualizer*`), Adaptive CPU
(`src/extra/adaptive_cpu*`), Unified Scenario Presets
(`src/extra/scenario_presets*`), Mid-Session Fighter Swap
(`src/extra/fighter_swap*`), native Brawl/P+ UI button emblems and stock-icon
rendering (`src/extra/ui_assets*`), menu option descriptions
(`src/extra/menu_descriptions.*`), and pure skill drill detection backends
(`src/extra/drill_*`). It is linked against the main module, so it calls the
main module's functions and reads its data directly, with no interface code in
between:

- `scripts/rel-exports.py` reads the main module's ELF after it is linked
  and writes `build/main/exports.lst`: one `21582,section,offset:name` line
  per global symbol. That is the form elf2rel takes for a symbol in another
  REL module (Syriinge plugins use it for symbols in the game's own modules).
  The extra module's REL is made with that list after the game's symbol lists.
- The linker drops main-module functions that nothing in the main module
  reaches. A function only the extra module calls must be marked with
  `#pragma force_active on` (see `src/main/extra.cpp`); otherwise elf2rel
  reports it as unresolved and the build stops.
- When the game's `OSLinkFixed` links the extra module, it applies the extra
  module's relocations against every module already linked, the main module
  included. So the stub loads it right after the main module, only if that
  one loaded, and unloads it first.
- The main module never names anything in the extra module. The extra module
  registers its hooks with the main module when it attaches (`main/extra.h`).
  It also logs one line per match from what it reads in the main module;
  `make EXTRA_PROOF=1` (into `build/proof/`) also draws that line.
- The load rule: Training matches (1P to 4P) and opted-in local offline Versus
  matches with Assist Trophies off (2P to 4P) load the extra module right after
  the main module into `AssistFigureResource` (MEM2, 419,424 B usable) while
  that heap holds no block. Because `AssistFigureResource` sits in MEM2
  (`0x92xxxxxx`..`0x93xxxxxx`, >32 MiB from MEM1), `scripts/pack-rel.py
  --far-calls extra` appends a 16-byte PowerPC far-call trampoline island
  (`lis r12; ori r12; mtctr r12; bctr`) per unique external `R_PPC_REL24`
  target to `extra`'s `.text` section, rewrites external call/tail-call sites
  to branch locally to their stubs, and replaces those `R_PPC_REL24` records
  with `R_PPC_ADDR16_HI`/`LO` pairs (with `R_DOLPHIN_NOP` spacers for >64 KiB
  deltas). Leaving `OverlayFighter4` untouched unlocks all four distinct
  fighter heap sets simultaneously; `src/stub/set_guard.cpp` self-disables when
  `extra` is not in `OverlayFighter1..4` and stays active as a safety net for
  `OverlayFighter4` fallback or `EXTRA_HEAP=56` builds. `src/main/extra.cpp`
  excludes `extra`'s `gfMemoryPool` payload from savestate snapshots via
  `savestate::setOwnExclusionAt(1, ...)`.
- `pack-rel.py` stops if a module imports from another packed module that
  the stub does not keep loaded for it (only extra from match is allowed),
  and prints which module each one links against.

## Source layout

Sources live under `src/`, each header next to its source file, and are
included by path from `src/` (`#include "shared/log.h"`). The stub is built
from `src/stub/` and `src/shared/`, the main module from `src/main/` and
`src/shared/`; each module compiles the shared sources itself (with
`TM_MODULE_STUB` or `TM_MODULE_MAIN` defined), and objects mirror the tree
under `build/stub/` and `build/main/`. `src/*/experiments/` is compiled only
by `make EXPERIMENTS=1` (into `build/experiments*/`, with `TM_EXPERIMENTS`
defined). All code is in namespace `tm`, with a sub-namespace per area.

| Folder      | Contents                                                        |
|-------------|-----------------------------------------------------------------|
| `src/shared/` | the stub/module interface, version, logging, destructor chain, read-only game queries (scene, game mode, frame counter, heaps), controller button maps (`game/pad_map.h`), the game's menu sounds (`game/menu_sound.h`) |
| `src/stub/` | plugin entry, REL glue, checked hooks and the inline-hook stub fix, hook dispatch, the packed file reader, the load policy, fighter-set guard and the loader |
| `src/stub/experiments/` | module-load log (`EXPERIMENTS=1` only) |
| `src/main/` | the main module (kind "match"): REL glue, attach/detach, the lab (`lab.cpp`: opening its menu, input capture, percent lock, frame advance, OSDs), its settings (`settings.*`), and the extra hook table (`extra.*`) |
| `src/main/game/` | match state, fighters, status names, motion, body values, trackers, frame advantage, technique timings, overlays, P+ Code Menu guard, in-match Wii pad mapping, and stopping the game (`freeze.*`) |
| `src/main/cpu/` | CPU control, DI/SDI/ASDI, tech/get-up logic, counter-action pools, Advanced Counter tables, and script source |
| `src/main/display/` | Display Options and General rows (Camera Mode, Game Speed, Move Staling): scene layers, stale queues, frame rate, camera, and Project+'s own hitbox and collision drawing |
| `src/main/gfx/` | 2D overlay drawing: rectangles, triangles, ellipses, text (left, right and centred) |
| `src/main/ui/` | table-driven menu (`menu.*`), lab menu over settings (`lab_menu.*`), native P+/Brawl styling (`style.h`, also used by the menu module), HUD layout (`hud_layout.*`), info panels, OSD messages, Ledgedash HUD, Brawl match timer (`match_hud.*`), watermark and debug overlay |
| `src/main/ui/pages/` | the lab menu's pages, one file per page, listed in `registry.cpp` |
| `src/main/record/` | 6-slot input recording/playback, Start On Input arming, weighted slot selection, mirrored playback, rewind, and recording seekbar HUD |
| `src/main/savestate/` | in-match savestate snapshot/restore and verification tracing |
| `src/main/storage/` | SD card file IO (`file_io.*`, also compiled into the menu module), file formats (`format.h`, `areas.*`, see [files.md](files.md)), settings persistence (`settings_file.*`), and recording export/import (`recording_file.*`) |
| `src/main/experiments/` | heap measurements and size padding (`EXPERIMENTS=1` only) |
| `src/menu/` | the menu module (kind "menu"): the Training Menu launcher (`drill_list.*`), session state (`session.*`), bests persistence (`bests_file.*`), and the recordings import browser (`browser.*`, `names.*`) |
| `src/menu/game/` | Training's character select and direct match launch (`training.*`, `select.*`) |
| `src/extra/` | the extra module (kind "extra"), linked against the main module: entry points (`rel.cpp`, `module.cpp`), Alter Inputs, DI Display, PSA frame data, Character RNG & Fighter Mechanics, Stage Options & geometry, PPlusAI recovery, Combo Trainer, Hitbox Heatmap, Grab/Tether Visualizer, Adaptive CPU, Scenario Presets, Fighter Swap, native UI assets, menu descriptions, and pure skill drill backends |

The plugin's version is set in `src/shared/version.h`.

## Hooks

Every patch to game code goes through `tm::hook` (`src/stub/hook.h`), and
only the stub patches. A hook site lists the instruction words expected at
its address on RSBE01 rev 2 with Project+ 3.2.1, read from memory in the
emulator. They are checked before anything is patched; on a mismatch the
plugin logs a line and leaves the game alone. The sites today:

| Site | Where | Used for |
|------|-------|----------|
| 0x8001792C | end of frame drawing | the module's `render` callback |
| 0x8002D248 | `gfSceneManager::process`, before the scene's `start()` | loading a module |
| 0x8002D288 | `gfSceneManager::process`, before the scene's `exit()` | unloading it |
| 0x8002A274 | `gfPadSystem::updateSystem`, after the system pads are copied into the debug pads, before menu pads and merged pads are made from them | the module's `pads` callback (menus) |
| 0x8002A7A8 | `gfPadSystem::updateGame`, after the game pads are filled, before they are merged; once per game step, before the step's tasks run | the module's `pads` callback (frame advance, the game's own readers of the game pads) |
| 0x8004AA70 | `ipSwitch::update`, once per player per game step: the game has copied the player's pad onto its stack and is about to turn it into that player's controls | the module's `fighterPad` callback (what a fighter must not see of its pad) |
| 0x8083ABDC | `Fighter::processFixCamera` | keeping overhead tag/arrow cursors off while an overlay hides a fighter |

The fighter-pad hook exists because of Project+: its "Controller Input Lag Fix"
(a branch at 0x8002AD8C) makes `gfPadSystem::getGamePadStatus` return the
port's system pad, the one the pad thread writes, in place of its game pad. So
nothing changed in the game pads at 0x8002A7A8 reaches a fighter; a button
is taken away from a fighter in the copy `ipSwitch::update` converts (and
whatever is held with it still arrives). Project+ also reads the analog
triggers straight from the system pad into the fighter's variables (LA-Float
36 and 37); the match module clears those from the fighter-input hook when
L or R is the Frame Advance button.

Inline hooks get two more checks. The `sy_core.rel` shipped with Project+
3.2.1 builds inline-hook stubs that restore r0 and LR in the wrong order, so
the hooked code resumes with the two exchanged. After installing an inline
hook, the plugin finds its stub, logs its words, and swaps the two restore
loads into the right order if, and only if, they are exactly the known-bad
pair. A stub with any other layout makes the plugin remove the hook again.
The stubs also do not save CR, CTR, XER, FPSCR or the floating-point
registers, so each hook enters through a small assembly routine
(`tmPreservingCall`) that saves the volatile ones before calling C++.
