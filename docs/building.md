# Building

Project+ Training Mode Mod (`P+TMM`) is built with Metrowerks CodeWarrior 4.2 (`mwcceppc`, `mwldeppc`) and `elf2rel` (the same toolchain used by Syriinge and `SyriingePluginTemplate`), plus Python 3.8 or newer (standard library only) to pack the modules into one file.

The build produces `build/TrainingMode.rel`: a compact stub plugin (module ID `21581`) with three scene-loaded modules (`match`, `menu`, and `extra`) appended to it. Because the toolchain binaries are Windows executables, these build instructions target Windows.

## Requirements

- [devkitPro](https://devkitpro.org/wiki/Getting_Started) with its bundled MSYS2 (`make`, `sh`, `sed`, `awk`, `tr`). `devkitPPC` itself is not needed and `DEVKITPPC` does not need to be set.
- Python 3.8 or newer (standard library only) for `scripts/pack-rel.py`, `scripts/rel-exports.py`, `scripts/verify_release.py`, and the `scripts/gen_*.py` table generators.
- Git (included with devkitPro MSYS2 or Git for Windows).
- Network access to `github.com` for the one-time `make tools` download.

Clone into a path without spaces; `make` does not handle spaces in build paths.

## Quick Start

Open devkitPro's MSYS2 shell (`msys2/msys2.exe` inside your devkitPro installation), then run:

```sh
git clone --recurse-submodules https://github.com/skierrz/pplus-training-mode.git pplus-training-mode
cd pplus-training-mode
make tools
make
```

If you cloned without `--recurse-submodules`, initialize submodules first:

```sh
git submodule update --init --recursive
```

`make tools` runs `scripts/fetch-tools.sh`, downloading CodeWarrior 4.2 and `elf2rel` from [SyriingePluginTemplate](https://github.com/Sammi-Husky/SyriingePluginTemplate) at a pinned commit into `tools/` (git-ignored) and verifying every file's SHA-256 hash. To copy them from a local checkout instead, run `make tools TOOLS_SRC=path/to/SyriingePluginTemplate`.

### Using PowerShell or Another Shell

Any Windows shell works as long as devkitPro's `msys2/usr/bin` folder appears ahead of other `make`/`sh` installations on `PATH`:

```powershell
$env:PATH = "<devkitPro>\msys2\usr\bin;" + $env:PATH
make
```

Or as a one-off command from any prompt:

```sh
<devkitPro>\msys2\usr\bin\bash.exe -lc "cd /c/path/to/pplus-training-mode && make"
```

## Build Configurations and Targets

| Command | Effect |
|---------|--------|
| `make` | Incremental release build (`build/TrainingMode.rel`, `STAGE4C=1`) |
| `make V=1` | Same, printing full compiler and linker commands |
| `make PERF=1` | Instrumented benchmark build (`build/perf/TrainingMode.rel`, used by `make PERF=1 test-perf`) |
| `make STAGE4C=0` | Build excluding Stage 4C challenge backends (`build/no-stage4c/TrainingMode.rel`) |
| `make verify-release` | Full release gate: builds `Normal`, `PERF=1`, and `STAGE4C=0` (0 warnings + budgets + `PERF=1` `main` headroom `>= 512 B`), runs `make check-host`, verifies all `scripts/gen_*.py --check` tables, and runs the public hygiene check |
| `make check-host` | Runs table generator checks, `test_pack_rel.py`, and native 32-bit Visual Studio PC logic tests into `build/host/` (no emulator or console toolchain required) |
| `make negative` | Builds deliberately broken plugins (`badversion`, `corrupt`, `truncated`, `extra-corrupt`) for `test-reject` and a 1 MiB padded `.rel` for `test-big-file` |
| `make clean` | Delete `build/` |
| `make tools` | Populate `tools/` (no-op if already up to date) |

Emulator targets (`make install`, `make run`, `make test-*`) are documented in [testing.md](testing.md).

## Memory Budgets and Compiler Flags

All intermediates (`.o`, `.dep`, `.elf`, `.map`, per-module `.rel`) live under `build/` (`build/stub/`, `build/main/`, `build/menu/`, `build/extra/`). On every build, `scripts/pack-rel.py` prints loaded footprints (`fixSize + .bss`, each rounded up to 32 bytes) and fails if any budget is exceeded:

| Budget Variable | Limit | Target Heap / Purpose |
|-----------------|-------|-----------------------|
| `STUB_BUDGET` | `20,480 B` (`20 KiB`) normal / `STAGE4C=0`; `24,576 B` (`24 KiB`) with `PERF=1` or `EXPERIMENTS=1` | 64 KiB Syriinge heap shared with `sy_core` and other P+ plugins (~50 KiB free before our stub) |
| `MAIN_BUDGET` | `292,832 B` | `OverlayMenu` permanent block for `match` (`main`) module |
| `MENU_BUDGET` | `98,304 B` (`96 KiB`) | `OverlayMenu` permanent block for `menu` module on Training CSS |
| `EXTRA_BUDGET` | `418,816 B` (`409 KiB`) | MEM2 `AssistFigureResource` (`419,424 B` usable, leaving 608 B margin) |
| `TEMP_BUDGET` | `495,616 B` (`484 KiB`) | Peak unpacked module file size during `OSLinkFixed` inside `MenuInstance` (`0x79A60` = `498,272 B` free at `scMelee` start) |

Code-size optimization details in `Makefile`:

- **`COMPACT_CXXFLAGS`:** `main`, `menu`, and `extra` compile with `-opt level=4,space,peephole,nostrength -inline on,noauto -ipa file -schedule off -func_align 4 -use_lmw_stmw on -str pool,reuse,readonly -fp_contract on`, while `global_chain.cpp` and each module's `rel.cpp` keep standard `CXXFLAGS`.
- **`INFO_EXTRA_SRCS`:** `src/main/ui/info_display.cpp` and `src/main/game/status_names.cpp` are compiled into the `extra` module rather than `main`, keeping strong `OverlayMenu` headroom even under `PERF=1`.
- **Generated compressed tables (`scripts/gen_*.py`):** Menu descriptions, page strings, PSA opcode names, and common status names are stored as compressed tables verified by `make check-host` and `make verify-release`.

## Local Settings

Copy `local.mk.example` to `local.mk` (git-ignored) to configure local tool or emulator paths. Never commit machine-specific paths.

## Installing

Copy `build/TrainingMode.rel` into `/Project+/pf/plugins/` on your Project+ v3.2.1 SD card (or virtual `sd.raw`). Keep the exact filename `TrainingMode.rel`: the stub opens `plugins/TrainingMode.rel` to load its appended modules.

Expected OSReport boot log lines (the stub logs from `_prolog` before Syriinge logs the loaded plugin):

```text
[TrainingMode] v1.0.1 loaded
...
[TrainingMode] modules: /Project+/pf/plugins/TrainingMode.rel: ok; ...
[TrainingMode] module MTCH: id 21582, ...: ok
[Syringe] Loaded plugin (TrainingMode, v1.0.1)
```

## The Stub and Its Modules

Because Project+ v3.2.1's Syriinge heap is only 64 KiB shared across all plugins, only the stub stays resident permanently. Scene-specific modules load on demand and unload cleanly on scene exit:

| Module | Kind, ID | Loaded For | Heap | Contents |
|--------|----------|------------|------|----------|
| `main` (`src/main/`) | `match`, `21582` | Every Training match and opted-in local offline Versus | `OverlayMenu` | Lab engine, Lab Menu, OSDs, trackers, CPU brain, Display Options, savestates, recording, SD storage |
| `extra` (`src/extra/` + `INFO_EXTRA_SRCS`) | `extra`, `21584` | Training matches (1P to 4P) and opted-in local offline Versus when Assist Trophies are off (2P to 4P), loaded right after `main` | `AssistFigureResource` (MEM2) | Info Display, status names, DI Display, PSA frame data & 11-thread inspector, Frame Meter / Action Log, Character RNG, Fighter Mechanics, Olimar Lab Kit, Stage Options, PPlusAI recovery, Combo Trainer, Hitbox Heatmap & Colors, Grab Visualizer, Input Viewer, Timing Meter, Tech Sparkle, Adaptive CPU, Scenario Presets, Fighter Swap, UI assets, menu descriptions & summaries, Credits modal, and skill drill / challenge engines |
| `menu` (`src/menu/`) | `menu`, `21583` | Training character select (`scSelctCharacter` in `sqTraining`) | `OverlayMenu` | 4-tab Boot Menu launcher, Quick Setup, Credits modal, bests persistence, and `.tmr` Recordings import browser |

- **Packed file format:** `scripts/pack-rel.py` appends each module `.rel` (32-byte aligned) after the stub `.rel`, followed by a module table (ID, kind tag, offset, size, FNV-1a checksum) and a 32-byte trailer (`src/stub/packed_file.h`). `gfModule::create` copies only the stub's first `fixSize` bytes, so the appended modules use zero Syriinge heap.
- **Boot verification:** The stub installs all 11 permanent inline hooks via `tm::hook` (the `main` and `menu` modules only apply checked, reversible runtime patches that restore cleanly on clear/detach), verifies the table and checksum of every appended module inside `plugins/TrainingMode.rel`, and logs the verdict.
- **Scene load policy (`src/stub/load_policy.cpp`):**
  - Training (`scMelee` with Training game mode): loads `match` into `OverlayMenu`, then `extra` into `AssistFigureResource` (only if empty).
  - Opted-in local offline Versus (`scMelee` in `sqVsMelee`, with Netplay GCT, Netplay VSync patch, and Replay mode all verified absent): loads `match` + `extra` when Assist Trophies are off, or `match` only when Assist Trophies are on so Brawl keeps `AssistFigureResource` free.
  - Training CSS (`scSelctCharacter` in `sqTraining`): loads `menu` into `OverlayMenu`.
- **Linking and unloading:** Each module is read via ranged SD IO into a temporary buffer in `MenuInstance`, linked with `OSLinkFixed` into its permanent target heap block (`fixSize + .bss`), cache-flushed, and its temporary buffer freed before `_prolog` and `attach` run. The stub passes a persistent 768-byte session block so settings survive module unloads between matches. Just before scene `exit()`, the stub clears callback slots and unloads modules in reverse order (`detach`, `_epilog`, `OSUnlink`, free) and logs heap baseline restoration.

## The Extra Module and Far-Call Trampolines

The `extra` module links directly against `main`'s global symbols (`build/main/exports.lst`, generated from `TrainingModeMain.elf` by `scripts/rel-exports.py`), after Brawl's symbol lists (`lib/BrawlHeaders/RSBE01.lst` and `link/EXTRA.lst`). Any `main` function called only by `extra` is marked `#pragma force_active on` so `mwldeppc` retains it.

Because `AssistFigureResource` resides in MEM2 (`0x92xxxxxx`..`0x93xxxxxx`, more than 32 MiB away from MEM1), `scripts/pack-rel.py --far-calls extra` appends a 16-byte PowerPC far-call trampoline island (`lis r12; ori r12; mtctr r12; bctr`) per unique external `R_PPC_REL24` target to `extra`'s `.text`, rewrites external call/tail-call sites to branch locally, and converts those relocations to `R_PPC_ADDR16_HI`/`LO` pairs (inserting `R_DOLPHIN_NOP` spacers for deltas exceeding 64 KiB). `src/main/extra.cpp` excludes `extra`'s `gfMemoryPool` payload from savestates via `savestate::setOwnExclusionAt(1, ...)`.

## Source Layout

All sources live under `src/` in namespace `tm` (with sub-namespaces per subsystem) and include project headers by path from `src/`.

| Folder | Contents |
|--------|----------|
| `src/common/` | Header-only components shared across `menu` and `extra`, including the scrollable GX Credits modal (`credits_modal.h`) |
| `src/shared/` | Stub/module interface, version (`version.h`), logging, destructor chain, perf helpers, read-only game queries, 4-controller button maps (`game/pad_map.h`), menu sound IDs |
| `src/stub/` | Plugin entry, REL glue, verified inline hooks + `sy_core` stub fix, dispatch, packed file reader, scene load policy (`load_policy.cpp`), fighter-set guard, and module loader |
| `src/main/` | `match` module entry, attach/detach, lab orchestrator (`lab.cpp`), settings (`settings.*`), and extra hook table (`extra.*`) |
| `src/main/game/` | Match state, fighters, status IDs & compressed status names (`status_names.cpp`, compiled into `extra`), motion, body reads, trackers, frame advantage, technique timings, overlays, Code Menu guard, in-match Wii pad mapping, and frame freeze |
| `src/main/cpu/` | Single-TU CPU brain (`brain.h`, `options.cpp`, `options_di_sdi.hpp`, `options_tech.hpp`, `options_counters.hpp`), action tables, tech/DI/SDI/SHFFL/clatter/ledge logic, and scripted input source |
| `src/main/display/` | Scene layer toggles, stale-move clearing, game speed, camera modes, and P+ hitbox/collision `renderDebug` callers (`debug_draw.cpp`) |
| `src/main/gfx/` | 2D GX shapes, hardware scissoring (`pushScissor`/`popScissor`), GX state reset, and Brawl font rendering |
| `src/main/ui/` | Table-driven Lab Menu engine (`menu.*`, `lab_menu.*`), native styling (`style.*`), two-layer semantic & Colorblind Mode palettes (`state_colors.h`), HUD layout, Info Display (`info_display.cpp`, compiled into `extra`), barless FIFO OSDs (`osd.cpp`), Ledgedash HUD, v1->v2 Update Saved Settings modal, Brawl match timer, and watermark |
| `src/main/ui/pages/` | Lab Menu pages (one file per page, registered in `registry.cpp`) |
| `src/main/record/` | 6-slot HMN/CPU input recording, playback, Start On Input, weighted random slots, mirroring, Frame Rewind, and seekbar HUD |
| `src/main/savestate/` | In-match RAM savestate snapshot/restore with real-Wii cache-tiled IRQ-yielding copy and verification tracing |
| `src/main/storage/` | Async SD card IO (`file_io.*`, also compiled into `menu`), file formats (`format.h`, `areas.*`), settings persistence (`settings_file.*`), and `.tmr` export/import (`recording_file.*`) |
| `src/menu/` | `menu` module: 4-tab Boot Menu launcher (`drill_list.*`), Quick Setup (`osd_quick.*`), session state, bests persistence (`bests_file.*`), and `.tmr` Recordings browser (`browser.*`, `names.*`) |
| `src/menu/game/` | Training CSS handling and direct match launch (`training.*`, `select.*`) |
| `src/extra/` | `extra` module: DI Display, PSA frame data & 11-thread inspector (`psa_inspector*`, `psa_opcodes.*`), Frame Meter (`action_log*`), Character RNG, Fighter Mechanics, Olimar Lab Kit (`olimar_lab.*`), Stage Options & geometry, PPlusAI recovery, Combo Trainer, Hitbox Heatmap & Colors, Grab Visualizer, Input Viewer, Timing Meter, Tech Sparkle, Adaptive CPU, Multi-Slot Reversals, Scenario Presets, Fighter Swap, UI assets, menu descriptions & live summaries (`menu_summaries.*`), Credits modal (`credits_modal.*`), and skill drill / challenge engines (`drill_*`, `challenge_backends.*`) |

## Hooks and Runtime Patches

All permanent inline hooks run through `tm::hook` (`src/stub/hook.h`) inside the stub. Each hook verifies expected RSBE01 rev 2 / Project+ v3.2.1 instruction words before patching and aborts safely on mismatch:

| Address | Location | Purpose |
|---------|----------|---------|
| `0x8001792C` | End of frame rendering | Module `render` callback |
| `0x8002D248` | `gfSceneManager::process`, before scene `start()` | Scene module loading |
| `0x8002D288` | `gfSceneManager::process`, before scene `exit()` | Scene module unloading |
| `0x8002A274` | `gfPadSystem::updateSystem` | Module `pads` callback (menus, savestates) |
| `0x8002A7A8` | `gfPadSystem::updateGame` | Module `pads` callback (Frame Advance, game pad readers) |
| `0x8004AA70` | `ipSwitch::update` | Module `fighterPad` callback (capture and per-fighter button filtering) |
| `0x80764E9C` | `soControllerImpl::update` (`sora_melee`) | Module `fighterInput` callback (CPU control and recording playback injection) |
| `0x8083ABDC` | `Fighter::processFixCamera` (`sora_melee`) | Keeps overhead player/CPU cursors hidden while a fighter is hidden |
| `0x80946160` | `stLoaderPlayer::processBegin` (`sora_melee`) | Fighter heap-set guard |
| `0x80946348` | `stLoaderPlayer::processBegin` (`sora_melee`) | Drill swap guard (blocks Code Menu character swaps while a drill is active) |
| `0x806DD600` | `sqBoot::setNext` (`sora_scene`) | Boot-to-Training / hold `X` or `Y` for Versus override |

In addition to the stub's 11 permanent inline hooks, `main` and `menu` apply checked, reversible runtime patches (flushed with `DCFlushRange` + `ICInvalidateRange` and restored on rule clear or module detach):

- **`main`:** `src/main/game/training_damage.cpp` patches 3 instruction sites (`0x80962594`, `0x809625BC`, `0x809625EC` in `stOperatorInfoTraining::processBegin`) and `src/main/game/training_controller.cpp` patches 1 instruction site (`0x80962D0C`).
- **`menu`:** `src/menu/game/select.cpp` patches 8 code sites in `sora_menu_sel_char` (`0x806834D0`, `0x8068AE7C`, `0x80689DD8`, `0x8068AE1C`, `0x806895E8`, `0x80683170`, `0x8068B814`, `0x806892BC`) plus 8 layout-count table words at `0x806A0AFC` when a drill restricts Training's character select screen.

Because Project+'s Controller Input Lag Fix (`0x8002AD8C`) reads the port's system pad directly, per-fighter button suppression runs inside `ipSwitch::update` (and analog trigger variables LA-Float 36/37 are cleared when L or R is bound to Frame Advance). Inline hooks also validate and repair `sy_core.rel`'s reversed `r0`/`LR` restore sequence and route through `tmPreservingCall` to preserve volatile CR, CTR, XER, FPSCR, and floating-point registers.
