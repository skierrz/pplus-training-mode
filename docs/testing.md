# Testing

Project+ Training Mode Mod (`P+TMM`) has three verification layers:

1. **Release readiness gate (`make verify-release`):** builds all three configurations (`Normal`, `PERF=1`, `STAGE4C=0`) enforcing zero warnings and every memory budget (including `PERF=1` `main` headroom `>= 512 B`), runs `make check-host`, verifies all generated tables (`scripts/gen_*.py --check`), and runs the public repository hygiene scan.
2. **Host tests (`make check-host`):** compiles pure logic, UI, storage, layout, and text-fit modules natively with Visual Studio's 32-bit C++ compiler (`cl`) and executes in under a second with no emulator.
3. **Dolphin integration tests (`make test-*`):** runs Project+ v3.2.1 in Brawl Online's Dolphin build via its automation harness, driving menus and matches with scripted inputs and checking RAM, OSReport logs, and screenshots.

All Dolphin tests run inside an isolated project-only Dolphin user directory so your normal Dolphin settings, saves, and SD cards are never touched. Never run two Dolphin test targets simultaneously against the same `DOLPHIN_USER` directory.

## Release Readiness Gate

Run this before committing or cutting a release:

```sh
make verify-release
```

Pass `VERIFY_RELEASE_ARGS="--clean"` for a clean rebuild of all three configurations, or `VERIFY_RELEASE_ARGS="--host-dump path/to/mem1.bin"` to include real-RAM host tests.

## Host Tests (`make check-host`)

`make check-host` runs:

1. All four generator freshness checks (`gen_psa_opcodes.py`, `gen_page_strings.py`, `gen_menu_descriptions.py`, `gen_status_names.py` with `--check`).
2. `tests/host/test_pack_rel.py` (packed `.rel` writer/reader and far-call trampoline checks).
3. `tests/host/build.py`, which compiles `MOD_SOURCES` plus `tests/host/host/*.cpp` and `tests/host/tests/*.cpp` with 32-bit `cl` into `build/host/host_tests.exe` and runs every host test suite.

Key host coverage:

- **Menu, settings, and credits:** row bounds/defaults/alignment, `Option::desc == NULL` in `main` (descriptions live in `extra/menu_descriptions.cpp`), per-option/page/area resets, session-only vs. persistent fields, clamping, disabled/hidden row skipping, scrolling, random-input fuzzing, and the shared Credits modal (`test_menu.cpp`, `test_settings_reset.cpp`, `test_credits.cpp`).
- **Storage, bests, and recordings:** dual-slot alternation, checksum/bounds refusal, v1 -> v2 upgrade modal and silent v2 upgrades, oversized `kNewer` protection, area extension/generation resets, sequence wraparound, and `.tmr` validation (`test_storage.cpp`, `test_bests.cpp`).
- **Layout, themes, and text fit:** 5% CRT safe-area containment across 4:3 and 16:9, HUD non-overlap, Colorblind Mode and OSD theme palettes, Frame Meter classification, and checking every drawn string literal in `src/` against Brawl's font metrics (`test_hud_layout.cpp`, `test_osd_place.cpp`, `test_osd_theme.cpp`, `test_action_log.cpp`, `test_text_fit.cpp`, `text_rules.py`).
- **Core gameplay and extra logic:** PSA script walking and opcode decoding, overlay and tech invisibility rules, CPU actions/techs/SHFFL/reversals/Wave 2 state-machine helpers, PPlusAI recovery, Combo Trainer, Hitbox Heatmap, Grab Visualizer, Scenario Presets, Fighter Swap, Stage Options, Olimar Lab Kit, Character RNG, Fighter Mechanics, offline Versus load policy, Code Menu patching, REL loader safety, and engine bugfix regressions (`test_frame_data.cpp`, `test_psa_inspector.cpp`, `test_cpu_*.cpp`, `test_pplus_recovery.cpp`, `test_combo_trainer.cpp`, `test_olimar_lab.cpp`, `test_stage6b_vs.cpp`, `test_engine_bugfixes.cpp`, `test_loader.cpp`, `test_hook.cpp`, and more).
- **4-controller support (8-port GC + Wii):** button mapping, hot-swap/disconnect recovery, slow-mo press latching, and per-controller prompts across GameCube, Classic, Nunchuk, and Sideways Wii Remote (`test_drill_list.cpp`, `test_wii_lab.cpp`, `test_wii_savestate.cpp`, `test_wii_display.cpp`).

Useful flags:

- `HOST_ARGS="--only PATTERN"` to filter tests
- `HOST_ARGS="--log"` to print diagnostic details
- `HOST_ARGS="--dump PATH"` or `HOST_DUMP=PATH` to run memory-dump tests against a captured game RAM dump

## Dolphin Test Prerequisites and Setup

To run `make test-*` targets locally:

- Brawl Online's Dolphin build supporting `--harness-port`.
- Your NTSC-U Brawl disc image (`RSBE01` rev 2).
- The Project+ v3.2.1 Offline Launcher DOL and SD card image (`sd.raw`).
- Python 3.8 or newer (standard library plus Pillow for screenshot verification).
- About 5 GB of free disk space for `SD_FOLDER` and `SD_IMAGE`.

Setup:

1. Copy `local.mk.example` to `local.mk` and fill in the emulator paths (unquoted, forward slashes, kept outside the repository).
2. Extract the SD image once:

   ```sh
   make sd-folder
   ```

## Dolphin Targets

### Core, Lifecycle, and Hygiene

| Command | Effect |
|---------|--------|
| `make install` | Build and copy `TrainingMode.rel` into `SD_FOLDER`'s `pf/plugins/` |
| `make run` | Install and launch Dolphin interactively on a writable SD copy (`sd-run.raw`) |
| `make test-load` | Unattended boot: stub loads, hooks install, packed modules verify, no match module loaded |
| `make test-match` | Enter and exit a Training match: match/extra load, watermark draws, clean unload back to heap baseline |
| `make test-stress` | Back-to-back Training matches across characters checking heap baselines and tracker observers (`STRESS_ARGS`) |
| `make test-reject KIND=...` | Install broken plugin files (`badversion`, `corrupt`, `truncated`) and verify clean rejection |
| `make test-big-file` | Verify ranged SD loading on a 1 MiB padded `.rel` file |
| `make test-boot` | Boot shortcuts, multi-port Training Menu hand-over, and Versus exclusion (`BOOT_ARGS`) |
| `make test-extra-heaps` | Verify `OverlayFighter3`/`OverlayFighter4` stay free and `AssistFigureResource` holds only `extra` (`EXTRA_HEAPS_ARGS`) |
| `make test-fighter-sets` | Verify Code Menu 3rd- and 4th-fighter loads succeed with `extra` in `AssistFigureResource` |
| `make test-storage` | Multi-boot writable SD tests for settings persistence, bests, export/import, browser, and locked card (`STORAGE_ARGS`) |
| `make test-card CARD=...` | Validate a ready-made `sd.raw` image end to end (`CARD_ARGS`) |
| `make test-sounds` | Verify menu sound requests across Boot Menu, Recordings browser, and Lab Menu |
| `make test-harness` | Verify batched harness reads equal unbatched reads (`HARNESS_ARGS`) |
| `make match-compare` | Pixel-compare a default plugin match against a plugin-free baseline outside overlay regions |

### Gameplay, CPU, Savestates, and Recording

| Command | Effect |
|---------|--------|
| `make test-lab` | Drive the Lab Menu, Info Display, percent locks, D-Pad bindings, Frame Advance, timer, and Exit (`LAB_ARGS`) |
| `make test-displays` | Scripted attacks verifying fighter trackers and on-hit/on-shield frame advantage (`DISPLAYS_ARGS`) |
| `make test-osds` | Scripted techniques verifying OSDs, Ledgedash HUD, color overlays, and tech invisibility (`OSDS_ARGS`) |
| `make test-drills` | Boot Menu launcher on CSS, direct match launches, and character select handling (`DRILLS_ARGS`) |
| `make test-select` | Character/stage selection rules and costume handling (`SELECT_ARGS`) |
| `make test-cpu` | CPU behaviors, DI/SDI/ASDI, tech/get-up, counters, shields, grab escape, and Move/Freeze CPU (`CPU_ARGS`) |
| `make test-controls` | Frame Advance across all button bindings, verifying clean fighter input suppression (`CONTROLS_ARGS`) |
| `make test-di` | Compare DI Display predicted trajectories, slides, and bounces against real fighter motion (`DI_ARGS`) |
| `make test-frames` | Compare PSA frame data (`Act In` and shield advantage lookahead) against actual actionable frames (`FRAMES_ARGS`) |
| `make test-savestate` | Save/load determinism, replays, D-Pad bindings, refusals, and multi-character sweeps (`SAVESTATE_ARGS`) |
| `make test-charges` | Verify fighter charge and resource preservation across savestates (`CHARGES_ARGS`) |
| `make test-record` | Recording, playback, slot weights, mirrored playback, Alter Inputs, and Frame Rewind (`RECORD_ARGS`) |
| `make test-rng` | Peach turnip/forward smash, Mr. Game & Watch Judge, and Luigi Misfire (`RNG_ARGS`) |
| `make test-fighter-mechanics` | Per-fighter charge, resource, float, Waddle Toss, and Nana Throw overrides (`FIGHTER_MECH_ARGS`) |
| `make test-stage-options` | Live hazards, platform freeze/presets, and Fountain of Dreams heights (`STAGE_OPTIONS_ARGS`) |
| `make test-recovery-combo` | 44-fighter PPlusAI recovery and Combo Trainer resets and floating meters (`RECOVERY_COMBO_ARGS`) |
| `make test-scenarios` | Unified Scenario Presets, auto-resets, side flip, and SD snapshot isolation (`SCENARIOS_ARGS`) |
| `make test-fighter-swap` | Mid-session fighter and costume swaps, cleanup barriers, and post-swap savestates (`FIGHTER_SWAP_ARGS`) |
| `make test-match-hud` | Brawl's native match timer across lab matches, pause, HUD toggles, and savestates (`MATCH_HUD_ARGS`) |
| `make test-code-menu` | Project+ Code Menu shortcut blocking by default and live toggling (`CODE_MENU_ARGS`) |

### Visuals, Inspection, Drills, and Stage Suites

| Command | Effect |
|---------|--------|
| `make test-display` | Display Options (HUD, Hitboxes, Models, Stage Collision, Stage Model, Effects) and General Camera/Speed/Staling (`DISPLAY_ARGS`) |
| `make test-grab-visualizer` | 3D grab, search/tether, and item-pickup collision overlays (`GRAB_VIZ_ARGS`) |
| `make test-psa-inspector` | Live PSA AnimCmd 11-thread & FSM Inspector in 16:9 and 4:3 (`PSA_ARGS`) |
| `make test-hud-theme-meter` | Unified Frame Meter, Colorblind Mode, HUD themes, and v2 settings persistence (`HUD_THEME_METER_ARGS`) |
| `make test-stage3d` | Start On Input, CC hitstun, and Hitbox Heatmap (`STAGE3D_ARGS`) |
| `make test-stage3f` | On-Ledge options, clatter escape, and Adaptive CPU (`STAGE3F_ARGS`) |
| `make test-stage5d` | Ledge 5-grab, wall info rows, and Nana Throw (`STAGE5D_ARGS`) |
| `make test-stage4a-core` | Core movement skill drill engines (`STAGE4A_CORE_ARGS`) |
| `make test-stage4a-hover-ps` | Float/Hover/DJC and Powershield drill engines (`STAGE4A_HOVER_PS_ARGS`) |
| `make test-stage4a-pplus-tech` | P+ tech drill engines such as RAR and DACUS (`STAGE4A_PPLUS_TECH_ARGS`) |
| `make test-skill-drills-ui` | Boot Menu and Drill HUD visual/UI flows (`SKILL_DRILLS_UI_ARGS`) |
| `make STAGE4C=1 test-stage4c` | Stage 4C challenges and character trainers (`STAGE4C_ARGS`, requires `STAGE4C=1`) |
| `make test-overnight-tools` | Visual inspection of Live Input Viewer, Hitbox Colors, and DI Vector tools (`OVERNIGHT_TOOLS_ARGS`) |

### Strict Wii Compatibility, Performance, and Torture Suites

| Command | Effect |
|---------|--------|
| `make test-wii-strict` | Strict Wii-compatibility checks: MMU enabled, Fastmem off, non-JIT CPU core, DSP LLE, and 4-player MEM2 heap checks (`WII_STRICT_ARGS`) |
| `make PERF=1 test-perf` | Emulated Wii per-frame CPU and render benchmarks across overlays, savestates, and recording (`PERF_ARGS`, requires `PERF=1`) |
| `make test-torture` | 11-category live Dolphin stress and torture suite (`TORTURE_ARGS`, e.g. `--bucket 1..4` or `--case <name>`) |

## Harness Architecture Notes

- **Frame-exact scripted input (`scripts/plugin_input.py`):** Harness pad injection has roughly 1 frame of latency. Timing-sensitive tests write frame-exact scripts directly into the match module's script control block (`src/main/cpu/script_source.*`) so an input scheduled for frame `N` executes on exact game frame `N`.
- **Batched reads (`scripts/harness.py`):** Dolphin's automation harness processes pipelined requests in order on a paused frame. `Emu.watch()`, `Emu.step()`, `Emu.trace()`, and `Emu.advance_until()` batch memory reads alongside `frame_advance` to keep frame-by-frame tests fast and deterministic.
