# Third-Party Notices

This file records, file by file, every component copied or adapted from another project in Project+ Training Mode Mod (P+TMM), along with the license published by each upstream project. For a high-level overview including runtime calls and studied references, see [CREDITS.md](CREDITS.md).

- **Copied:** Text or code taken directly, possibly edited.
- **Adapted:** An algorithm, rule, layout, or data table re-implemented after reading the upstream source.

Where an upstream repository publishes no license file, this document states so explicitly.

## SyriingePluginTemplate

- **Upstream:** https://github.com/Sammi-Husky/SyriingePluginTemplate
- **Commit:** `2b3ba22c5be9a29d2f7d33f98f1fd31c74f7df24`
- **License:** MIT (below)

| File | Upstream file | Notes |
|---|---|---|
| `Makefile` | `Makefile` | Compiler/linker flags, `elf2rel` step |
| `link/rel.lcf` | `rel.lcf` | Whitespace only |
| `link/EXTRA.lst` | `EXTRA.lst` | Header comments and format |
| `src/stub/rel.cpp` | `source/rel.cpp` | REL entry points |
| `src/main/rel.cpp`, `src/menu/rel.cpp`, `src/extra/rel.cpp` | `source/rel.cpp` | REL entry points, same pattern |
| `lib/sy_core/include/sy_core.h` | `lib/sy_core/include/sy_core.h` | Unmodified (Syriinge 0.6.0 SDK) |
| `lib/sy_core/include/version.h` | `lib/sy_core/include/version.h` | Unmodified (Syriinge 0.6.0 SDK) |

The two `sy_core` headers originate from [Syriinge](https://github.com/Sammi-Husky/Syriinge), also MIT licensed (`Copyright (c) 2023 - 2025 Sammi Husky`).

The template's `tools/` folder (Metrowerks CodeWarrior 4.2 compiler/linker and `elf2rel`) is not included in this repository and is not covered by the MIT license below. `scripts/fetch-tools.sh` downloads it at the pinned commit above.

```text
MIT License

Copyright (c) 2024 Sammi Husky

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## ProjectPunch

- **Upstream:** https://github.com/WispSSBM/ProjectPunch
- **Commit:** `f7930d25483f2a697d3f04b55d4287dc0fb9732c`
- **License:** MIT (below), used with Wisp's blessing

No code is copied verbatim. These algorithms, settings, and values are adapted cleanly onto P+TMM:

| File | Upstream file | What |
|---|---|---|
| `src/main/lab.cpp` | `source/pp/frame_advance.cpp` | Frame advance: single-frame press, hold past delay to run continuously, button stripped from fighter pad data |
| `src/main/lab.cpp` | `source/pp/ui.cpp` | Per-direction menu key repeat (fire, wait, repeat) |
| `src/main/ui/pages/controls.cpp` | `source/pp/ui.cpp`, `source/pp/global_settings.cpp` | Frame Advance hold delay setting (`Hold Repeat Delay`), range 5 to 60 frames |
| `src/main/game/freeze.cpp` | `source/pp/main.cpp`, `source/pp/entry.cpp`, `include/pp/main.h` | Stopping game tasks via bit `0x8` of `gfTaskScheduler` byte `+0xB` |
| `src/main/game/tracker.cpp` | `source/pp/status_change_watcher.cpp`, `source/pp/anim_cmd_watcher.cpp`, `source/pp/playerdata.cpp`, `include/pp/anim_cmd_utils.h` | Per-fighter status-change and animation-command observers; "actionable at any point this frame" tracking; interrupt-group and cancel commands |
| `src/main/game/advantage.cpp`, `src/main/game/advantage.h` | `source/pp/main.cpp`, `source/pp/playerdata.cpp` | On-hit / on-shield frame advantage: exchange start, first actionable frames, gap tolerance, re-arming, and display limit |
| `src/main/ui/pages/osd_menu.cpp` | `source/pp/ui.cpp`, `source/pp/global_settings.cpp` | `Advantage Tolerance` setting: range 0 to 5, default 1 |
| `src/main/game/status_names.cpp` | `include/pp/actions.h` | Names of the fighter-specific status range (`SpecialN` to `Final`, compiled into `extra` via `INFO_EXTRA_SRCS`) |
| `src/main/game/motion.cpp` | `source/pp/main.cpp` | Reading animation name and frame length |
| `src/main/game/body.cpp` | `include/pp/common.h` | Status work RA bits for short hop (6) and landing lag (30) |
| `src/main/game/techniques.cpp`, `src/extra/drill_ledge_core.h` | `source/pp/ledge_tech.cpp`, `source/pp/wavedash_info.cpp` | Ledgedash tracking (per-frame phase types from `CliffWait` to actionable landing, GALINT below zero, drop-off frame) and wavedash timing across jumpsquat, jump, airdodge, and special landing |
| `src/main/ui/ledgedash_hud.cpp`, `src/main/ui/ledgedash_hud.h` | `source/pp/ledge_tech.cpp` | Ledgedash frame bar (one cell per frame, phase categories, GALINT/ledge/angle summary), rendered with P+TMM's unified action-state palette (`src/main/ui/state_colors.h`) |
| `src/main/ui/pages/ledgedash_hud.cpp` | `source/pp/ui.cpp`, `source/pp/global_settings.cpp` | `Max Frames` setting: range 30 to 64, default 50 |
| `src/main/game/overlays.cpp` | `source/pp/color_overlay.cpp`, `include/pp/color_overlay.h`, `source/pp/invis_techs.cpp` | Tinting a fighter via `soColorBlendModule::setSubColor`; tech/getup subactions and frame windows hidden by invisible techs; hiding overhead tags from `Fighter::processFixCamera` (without using ProjectPunch's code patches) |
| `src/main/ui/pages/color_overlays.cpp` | `source/pp/color_overlay.cpp` | The `Standard (PPunch)` overlay preset (`Wait` Green, `Dash` Cyan, `Run` Blue, `IASA` Orange) |
| `src/main/ui/info_display.cpp`, `src/main/ui/info_display.h`, `src/main/ui/pages/info_display.cpp` | `source/pp/playerdata.cpp`, `source/pp/main.cpp`, `include/pp/anim_cmd_utils.h` | `Subaction` and `Cancel Flags` rows and the `Frame Data` preset (short group names are P+TMM's own) |

ProjectPunch's own drawing, text-printing, and vector files were ported from other projects upstream; nothing from those files is used in this repository.

## ilikepizza107 (Partially Invisible Techs)

- **Upstream:** https://gamebanana.com/scripts/12025 (also adapted in ProjectPunch's `source/pp/invis_techs.cpp`)
- **License:** No license file is published upstream.

No code is copied verbatim. The invisible-tech subaction/frame-hiding design is adapted in `src/main/game/overlays.cpp` (with settings in `src/main/ui/pages/tech_options.cpp`) alongside the ProjectPunch adaptation above.

```text
MIT License

Copyright (c) 2024 Wisp

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## lil klon's Dummy Tools for ProjectPunch

- **Source:** Private handoff package shared by lil klon (patch against ProjectPunch `main` at `bf53bb3`, plus Project+ 3.2.1 notes), used and adapted with permission and credit.

No code is copied verbatim. These behaviors are re-implemented cleanly onto Project+ 3.2.1 recording and CPU options:

| File | Upstream file | What |
|---|---|---|
| `src/main/record/start_input.h`, `src/main/record/record.cpp`, `src/main/record/hud.cpp`, `src/main/ui/pages/recording.cpp` | `extension/source/recorder.cpp` | Start On Input recording arming: hold capture until first non-neutral input, displaying an Armed HUD indicator |
| `src/main/cpu/ledge_logic.h`, `src/main/cpu/brain.h`, `src/main/cpu/options.cpp`, `src/main/cpu/options_counters.hpp` | `extension/source/recorder.cpp`, `extension/source/actions.cpp` | On-Ledge reversal trigger on actionable `Cliff_Wait` (`0x75`) frames, respecting delay frames and context-aware ledge options |
| `src/extra/reversal_slots_core.h`, `src/extra/reversal_slots.cpp`, `src/main/ui/pages/cpu_options.cpp` | `extension/source/actions.cpp` | Weighted multi-slot reversal picking and per-slot delays across On-Hit, On-Shield, On-Getup, and On-Ledge triggers |

## fudgepop01 (PPlusAI and SuperTraining)

- **Upstreams:**
  - https://github.com/fudgepop01/PPlusAI (44-fighter `Chars/*/ai_*/103_recoveryHub.as`, `Include/shared/templates/103_recoveryHub.as`, `Include/shared/snippets/103_recoveryHub.as`)
  - https://github.com/Fracture17/ProjectMCodes (`Codes/SuperTraining/` only, sole-authored by fudgepop01; no Fracture17 `Libraries/`, `BuildSystem/`, or `menu.cpp`, and no third-party Eon/Bero snippets from `hitboxHeatmap.cpp` are used)
- **License:** No license file is published upstream; adapted with express permission from fudgepop01 with credit.

No code is copied verbatim. All algorithms and per-fighter tables are cleanly re-implemented in C++ against Project+ 3.2.1 runtime structures:

| File | Upstream file | What |
|---|---|---|
| `src/extra/pplus_recovery_core.h`, `src/extra/pplus_recovery_core.cpp`, `src/extra/pplus_recovery.h`, `src/extra/pplus_recovery.cpp` | `PPlusAI` `103_recoveryHub.as`, `Codes/SuperTraining/WeightedDie.*` | 44-fighter recovery geometry table across 8 recovery families, stack-only `WeightedDie`, high-recovery mixup roll, 360-aim notch clamping, and glide/float/tether/charged/PK Thunder orbit recovery logic |
| `src/extra/combo_trainer_core.h`, `src/extra/combo_trainer.h`, `src/extra/combo_trainer.cpp`, `src/main/ui/pages/combo_trainer.cpp` | `Codes/SuperTraining/main.cpp`, `Codes/SuperTraining/FudgeMenu.h` | Combo Trainer hitstun-drop auto-reset, `comboTimerAdjustment`, `noclip` reset (with explicit 1-frame `Correct_Air` restore), `recoveryTrainer` offstage loop, and 7 floating world-space state meters |
| `src/extra/hitbox_heatmap_core.h`, `src/extra/hitbox_heatmap.h`, `src/extra/hitbox_heatmap.cpp`, `src/main/ui/pages/display_options.cpp` | `Codes/SuperTraining/hitboxHeatmap.cpp` | Fixed-ring hitbox trail accumulation, actionable-ground frame-age color progression, opacity/bounds controls, and Z-plane reach bounds (rendered via stock Project+ 3.2.1's `soCollision::getShape` and debug bubble routine) |
| `src/extra/adaptive_cpu_core.h`, `src/extra/adaptive_cpu.h`, `src/extra/adaptive_cpu.cpp` | `Codes/SuperTraining/MovementTracker.h`, `Codes/SuperTraining/MovementTracker.cpp`, `Codes/SuperTraining/PatternManager.h` | 128-entry static circular `MovementTracker` with similarity-weighted Markov probabilities (`approxChance`, `approxCommit`) and 15-entry weighted `PatternManager` RPS habit tracker (with wrap-around and start-index fixes) |
| `src/extra/psa_inspector_core.h`, `src/extra/psa_inspector.h`, `src/extra/psa_inspector.cpp`, `src/extra/psa_opcodes.h`, `src/extra/psa_opcodes.cpp` | `Codes/SuperTraining/` (`PSADataPage` / `PSAScriptPage` specification) | Read-only PSA AnimCmd 11-Thread & FSM Inspector layout, frame speed modifier readout, AnimCmd module prefixes, opcode names, and typed argument decoding specification |

## Brawl Online

- **Upstream:** https://github.com/sarahkittyy/brawlonline.net (https://brawlonline.net)
- **Commit read:** `af57a23ad3afbc7c04e8421ba906704a0aa105c5`
- **Licenses:** Per upstream `NOTICE`: `dolphin/` is GPL-2.0-or-later, `launcher/` is GPL-3.0, `game-code/` is MIT (with `PPOnline/` carrying `Copyright (c) 2023 Sammi Husky`), and `server/` is GPL-2.0-or-later. No license statement was found for `harness/`, `tools/`, or `docs/`.

Used with permission from Brawl Online's maintainer (sarahkittyy). No code is copied verbatim:

| File | Upstream file | What |
|---|---|---|
| `src/main/savestate/snapshot.cpp` | `dolphin/Data/Sys/Rollback/gp-v*.json` (GPL-2.0-or-later) | Fixed memory ranges and baseline exclusions copied by savestates (plus P+ 3.2.1 SD stream/SDHC exclusions added here) |
| `scripts/nav.py` | `harness/ppharness/brawl.py` (no license found) | CSS/SSS offsets, player-state values, and cursor-steering concept (steering logic rewritten here; upstream credits several offsets to Orca) |
| `scripts/fatimage.py` | `tools/sdcard/fat32.py` (no license found) | Re-implemented FAT32 test image writer following the same helper layout and constants |
| `src/stub/inline_stub.cpp` | `game-code/PPOnlineLoader/source/loader.cpp` (MIT), `docs/game-code.md` | Method only: repairing Project+ 3.2.1 `sy_core`'s swapped `r0`/`LR` restore instructions in inline-hook stubs |
| `src/stub/loader.cpp`, `src/stub/packed_file.cpp`, `scripts/pack-rel.py` | `game-code/PPOnlineLoader/` (MIT) | Method only: appending additional REL modules to a small stub plugin and linking them into dedicated heaps |

## Brawlback

Brawl Online builds on two Brawlback projects, so material derived from Brawl Online's `dolphin/` and `game-code/` trees carries their upstream notices:

- **Project-Plus-Dolphin:** https://github.com/Brawlback-Team/Project-Plus-Dolphin (`GPL-2.0-or-later`, crediting WhiteTPoison, PiNE / FaultyPine, Kapedani, eliasreid, rapito, Believer1, and contributors). Source of the rollback memory range list referenced by `src/main/savestate/snapshot.cpp`. The emulator itself is used only to run tests and is not shipped in this repository.
- **brawlback-asm:** https://github.com/Brawlback-Team/brawlback-asm (`MIT`, `Copyright (c) 2023 Sammi Husky`). No code is copied; only the packed-loader method noted under Brawl Online was studied.

## Project+

- **Upstream:** https://projectplusgame.com, by the Project+ Development Team
- **Version:** 3.2.1
- **License:** No license file is published with Project+.

No Project+ game files are included in this repository. P+TMM calls Project+'s own runtime routines in memory (listed in [CREDITS.md](CREDITS.md)). One helper loop is re-implemented: `src/main/display/debug_draw.cpp` mirrors the collision list walk of Project+'s stage collision display (`renderDebug/[grCollisionManager]` by Eon in `Source/Project+/Debug/Stage Collisions.asm`) and invokes Project+'s own drawing routine for each active entry.

## Submodules

Referenced as a git submodule (build-time headers and `RSBE01.lst` symbol list only; no source code is compiled into the plugin):

- **`lib/BrawlHeaders`:** https://github.com/Sammi-Husky/BrawlHeaders at commit `3ce22366a70c126692e8ad2a252d90918858982c` (mirrors `doldecomp/brawl`'s `include/` and includes OpenRVL headers; no license file is published upstream).

## doldecomp/brawl

- **Upstream:** https://github.com/doldecomp/brawl
- **License:** CC0-1.0 (public domain dedication)

No code is included. The 32 symbol addresses in `link/EXTRA.lst` are taken from its RSBE01 rev 2 symbol maps (`config/RSBE01_02/symbols.txt` and `config/RSBE01_02/rels/sora_melee/symbols.txt`):

- **OS & task scheduler:** `OSLinkFixed`, `OSDisableInterrupts`, `OSRestoreInterrupts`, `OSSleepTicks`, `OSTicksToCalendarTime`, `gfTaskScheduler::setPauseCategory`
- **Brawl fighter & pad APIs:** `soAnimCmdEventObserver::addObserver`, `ftCommonDataAccesser::getData`, `g_ftCommonDataAccesser`, `ipPadConfig::getInstance`
- **Wii GX rendering, textures, TEV & hardware scissor:** `GXDrawDone`, `GXInvalidateVtxCache`, `GXInvalidateTexAll`, `GXLoadPosMtxImm`, `GXInitTexObjCI`, `GXInitTexObjLOD`, `GXLoadTexObj`, `GXInitTlutObj`, `GXLoadTlut`, `GXSetZCompLoc`, `GXSetTevSwapMode`, `GXSetTevSwapModeTable`, `GXSetTevDirect`, `GXSetTevColorIn`, `GXSetTevAlphaIn`, `GXSetTevColorOp`, `GXSetTevAlphaOp`, `GXSetTevOrder`, `GXSetNumTevStages`, `GXGetViewportv`, `GXSetScissor`, `GXGetScissor`

Two additional symbols in `link/EXTRA.lst`, `gfFileIO::checkSDInserted` (`0x8001bcfc`) and `gfFileIO::checkSDWriteProtected` (`0x8001bcbc`), are unnamed in `doldecomp/brawl` (`fn_8001BCFC`, `fn_8001BCBC`) and were identified directly from RSBE01 rev 2 code.
