# Testing

The plugin has two test suites:

1. **Host tests (`make check-host`)**: compile selected C++ sources natively
   with Visual Studio's 32-bit compiler (`cl`) against stand-in headers and run
   in under a second with no emulator.
2. **Dolphin tests (`make test-*`)**: run Project+ 3.2.1 in Brawl Online's
   Dolphin build with its automation harness, driving menus and matches with
   scripted inputs and checking memory, logs, and screenshots.

Everything runs from a project-only Dolphin user folder, so your normal
Dolphin settings, saves and SD card are never touched.

## What you need (Dolphin tests)

- A Dolphin build that runs Project+ (Brawl Online's build with `--harness-port`).
- Your Brawl disc image (NTSC-U, RSBE01 rev 2).
- The Project+ release's Offline Launcher DOL and its SD card image (`sd.raw`).
- Python 3.8 or newer (standard library + Pillow for screenshot checks).
- About 5 GB of free disk space for `SD_FOLDER` and `SD_IMAGE`.

## Setup

1. Copy `local.mk.example` to `local.mk` and fill in the emulator section.
   Paths may contain spaces; write them unquoted, with forward slashes. Keep
   `DOLPHIN_USER`, `SD_FOLDER` and `SD_IMAGE` outside the repo.
2. Extract the SD card into a folder once:

   ```sh
   make sd-folder
   ```

## Targets

| Command | Effect |
|---------|--------|
| `make check-host` | PC logic, layout, text-fit, storage, frame-data, Code Menu, Wii pad, and loader tests (no emulator) |
| `make install` | build and copy `TrainingMode.rel` into `SD_FOLDER`'s `pf/plugins` |
| `make run` | install and launch Dolphin interactively on a writable SD copy (`sd-run.raw`) |
| `make test-load` | unattended boot: stub loads, hooks install, packed modules verify, no match module loaded |
| `make test-match` | play into a Training match and out again: match/extra load, watermark draws, clean unload back to heap baseline |
| `make test-stress` | multiple back-to-back Training matches across characters, checking heap baselines and tracker observers |
| `make test-reject KIND=...` | install deliberately broken plugin files and expect clean rejection |
| `make test-lab` | drive the lab menu, info display, percent locks, D-pad bindings, frame advance, match timer, and Exit |
| `make test-displays` | scripted attacks verifying fighter trackers and on-hit/on-shield frame advantage against memory |
| `make test-osds` | scripted techniques verifying OSDs, Ledgedash HUD, color overlays, and tech invisibility against memory |
| `make test-drills` | Training Menu launcher on CSS, direct match starts, and character select handling |
| `make test-cpu` | CPU options (behaviors, DI/SDI/ASDI, tech/get-up, counters, shields, grab escape, Move/Freeze CPU) |
| `make test-display` | Display Options (Match HUD, Hitboxes, Fighter Models, Stage Collision, Stage Model, Effects) and General Camera/Speed/Staling |
| `make test-controls` | Frame Advance across all button bindings (ensuring button never leaks to fighter while chords pass through) |
| `make test-di` | compare DI Display predicted trajectories, stage contacts, slides, and bounces against real fighter motion |
| `make test-frames` | compare PSA frame data (`Act In` and shield advantage lookahead) against actual actionable frames |
| `make test-savestate` | save/load determinism, replays, D-pad bindings, refusals, and multi-character sweeps (`SAVESTATE_ARGS`) |
| `make test-record` | input recording, playback, slot weights, mirrored playback, Modify Inputs, and Frame Rewind (`RECORD_ARGS`) |
| `make test-big-file` | verify ranged SD loading on a 1 MiB padded `.rel` file |
| `make test-extra-heaps` | verify `OverlayFighter3` and `OverlayFighter4` stay untouched across Kirby copies, items, and 3 CPUs |
| `make test-fighter-sets` | verify Code Menu 3rd-character load allowed and 4th-character load blocked while extra is loaded |
| `make test-storage` | multi-boot writable SD tests for settings persistence, bests, export/import, browser, and locked card |
| `make test-card CARD=...` | validate a ready-made `sd.raw` image end to end |
| `make test-sounds` | verify menu sound requests across Training Menu, Recordings browser, and lab menu |
| `make test-rng` | verify Character RNG for Peach (turnips/fsmash), Mr. Game & Watch (Judge), and Luigi (misfire) |
| `make test-fighter-mechanics` | verify per-fighter charge, resource, float, and Waddle Toss overrides across supported fighters |
| `make test-stage-options` | verify live stage hazards, platform freeze/presets, and Fountain of Dreams heights across stages |
| `make test-recovery-combo` | verify 44-fighter PPlusAI offstage recovery and Combo Trainer resets and floating meters |
| `make test-scenarios` | verify all 10 unified Scenario Presets, auto-resets, side flip, and SD snapshot isolation |
| `make test-fighter-swap` | verify mid-session fighter and costume swaps, cleanup barriers, and post-swap savestates |
| `make test-grab-visualizer` | verify 3D grab, search/tether, and item-pickup collision overlays |
| `make test-match-hud` | verify Brawl's native match timer across lab matches, pause, HUD toggles, and savestates |
| `make test-code-menu` | verify P+ Code Menu shortcut blocking by default and live toggling via Controls |
| `make test-charges` | verify fighter charge/resource preservation across savestates |
| `make test-boot` | verify boot shortcuts, multi-port Training Menu hand-over, and Versus exclusion |
| `make test-perf` | measure emulated Wii per-frame timings across overlays, savestates, and recording (`PERF=1`) |
| `make test-harness` | verify batched harness reads equal unbatched reads |
| `make match-compare` | pixel-compare a default plugin match against a plugin-free baseline outside watermark/overlay rects |

## Host tests (`make check-host`)

`tests/host/build.py` compiles the pure-logic and UI/storage sources listed in
`MOD_SOURCES` with 32-bit `cl` against stand-in headers (`tests/host/include/`)
and fake game objects (`tests/host/host/`), then runs `build/host/host_tests.exe`.

Key coverage:

- **Menu & settings (`test_menu.cpp`, `test_settings_reset.cpp`):** every row's
  bounds/alignment/default, keeping inline `Option::desc` NULL (descriptions
  live in `extra/menu_descriptions.cpp`), per-area/page/option resets,
  persistence vs session-only fields, clamping, disabled/hidden row skipping,
  scrolling, and random-input fuzzing.
- **Storage & recordings (`test_storage.cpp`, `test_bests.cpp`):** dual-slot
  settings/bests alternation, checksum/bounds refusal, newer-version safety,
  area extension and generation reset, slot encoding, and `.tmr` export/import
  validation.
- **Layout & text fit (`test_hud_layout.cpp`, `test_osd_place.cpp`,
  `test_text_fit.cpp`, `text_rules.py`):** 5% CRT safe-area containment,
  non-overlap across 4:3 and 16:9 scenes, native 640x480 OSD placement, and
  verifying every string literal in `src/` fits its box/buffer with Brawl's
  font metrics (guarded by function token checksums in `text_rules.py`).
- **Core gameplay logic (`test_frame_data.cpp`, `test_overlay_pick.cpp`,
  `test_cpu_actions.cpp`, `test_cpu_tech.cpp`, `test_player_shields.cpp`,
  `test_character_rng.cpp`, `test_code_menu.cpp`, `test_training_damage.cpp`,
  `test_tracker_swap.cpp`, `test_loader.cpp`, `test_hook.cpp`):** PSA script
  walking, overlay selection and tech invisibility rules, P+ CPU actions/techs,
  Code Menu patching, and REL loader safety.
- **8-port GC & Wii controllers (`test_drill_list.cpp`, `test_wii_lab.cpp`,
  `test_wii_savestate.cpp`, `test_wii_display.cpp`):** controller-type button
  mapping, hot-swap/disconnect recovery, slow-mo press latching, and
  per-controller prompts across GC, Classic, Nunchuk, and Sideways Wii Remote.

Pass `HOST_ARGS="--only PATTERN"` to filter tests, `HOST_ARGS="--log"` to
print diagnostic logs, or `HOST_ARGS="--dump PATH"` to run memory-dump tests
against a real game RAM dump.

## Frame-exact input (`scripts/plugin_input.py`)

Harness pad input has roughly 1 frame of latency. Timing-sensitive tests write
frame-exact scripts directly into the match module's script control block
(`src/main/cpu/script_source.*`) so an input scheduled for driven frame `N`
acts on exact game frame `N`.

## Batched harness reads (`scripts/harness.py`)

Dolphin's automation harness processes pipelined requests in order against a
paused frame. `Emu.watch()`, `Emu.step()`, `Emu.trace()`, and
`Emu.advance_until()` batch memory reads alongside `frame_advance` to keep
frame-by-frame tests fast and deterministic.
