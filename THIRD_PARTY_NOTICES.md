# Third-party notices

This file records, file by file, what this repository copies or adapts from
other projects, and the licence each of those projects publishes. For a
readable overview of whose work the mod stands on, including projects it only
calls at run time or learned from, see [CREDITS.md](CREDITS.md).

"Copied" means text or code was taken over, possibly edited. "Adapted" means
an algorithm, rule, layout or table was re-implemented from reading the other
project's source. Where a project publishes no licence, this file says so.

## SyriingePluginTemplate

- Upstream: https://github.com/Sammi-Husky/SyriingePluginTemplate
- Commit: 2b3ba22c5be9a29d2f7d33f98f1fd31c74f7df24
- Licence: MIT (below)

Adapted or copied into this repository:

| File                              | Upstream file                   | Notes                         |
|-----------------------------------|---------------------------------|-------------------------------|
| `Makefile`                        | `Makefile`                      | compiler/linker flags, elf2rel step |
| `link/rel.lcf`                    | `rel.lcf`                       | whitespace only               |
| `link/EXTRA.lst`                  | `EXTRA.lst`                     | comments                      |
| `src/stub/rel.cpp`                | `source/rel.cpp`                | REL entry points              |
| `src/main/rel.cpp`, `src/menu/rel.cpp`, `src/extra/rel.cpp` | `source/rel.cpp` | REL entry points, same pattern |
| `lib/sy_core/include/sy_core.h`   | `lib/sy_core/include/sy_core.h` | unmodified (Syriinge 0.6.0 SDK) |
| `lib/sy_core/include/version.h`   | `lib/sy_core/include/version.h` | unmodified (Syriinge 0.6.0 SDK) |

The two `sy_core` headers come from [Syriinge](https://github.com/Sammi-Husky/Syriinge),
which is also MIT licensed ("Copyright (c) 2023 - 2025 Sammi Husky").

The template's `tools/` folder (Metrowerks CodeWarrior compiler and linker,
`elf2rel`) is not part of this repository and is not covered by the licence
below. `scripts/fetch-tools.sh` downloads it from the upstream commit above.

```
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

- Upstream: https://github.com/WispSSBM/ProjectPunch
- Commit: f7930d25483f2a697d3f04b55d4287dc0fb9732c
- Licence: MIT (below)

No code is copied. These algorithms, settings and values are adapted, as
described in comments at the places that use them:

| File | Upstream file | What |
|------|---------------|------|
| `src/main/lab.cpp` | `source/pp/frame_advance.cpp` | frame advance: a press of the button runs one frame, holding it past a delay runs the game; the button is removed from the pad data the game reads |
| `src/main/lab.cpp` | `source/pp/ui.cpp` | menu key repeat kept per direction (fire, wait, then repeat) |
| `src/main/ui/pages/controls.cpp` | `source/pp/ui.cpp`, `source/pp/global_settings.cpp` | the Frame Advance hold delay setting and its range of 5 to 60 frames |
| `src/main/game/freeze.cpp` | `source/pp/main.cpp`, `source/pp/entry.cpp`, `include/pp/main.h` | stopping the game's tasks with bit 0x8 of the task scheduler's byte +0xB |
| `src/main/game/tracker.cpp` | `source/pp/status_change_watcher.cpp`, `source/pp/anim_cmd_watcher.cpp`, `source/pp/playerdata.cpp`, `include/pp/anim_cmd_utils.h` | per-fighter status-change and animation-command observers on the fighter's event units; "actionable at any point this frame" tracking; the interrupt-group and cancel commands |
| `src/main/game/advantage.cpp`, `src/main/game/advantage.h` | `source/pp/main.cpp`, `source/pp/playerdata.cpp` | on-hit / on-shield frame advantage: exchange start, both sides' first actionable frames, the gap tolerance, re-arming, the display limit |
| `src/main/ui/pages/osd_menu.cpp` | `source/pp/ui.cpp`, `source/pp/global_settings.cpp` | the Advantage Tolerance setting (ProjectPunch's minimum for actionable out of shield): range 0 to 5, default 1 |
| `src/main/game/status_names.cpp` | `include/pp/actions.h` | names of the fighter-specific status range (SpecialN to Final) |
| `src/main/game/motion.cpp` | `source/pp/main.cpp` | where the animation's name and length are read |
| `src/main/game/body.cpp` | `include/pp/common.h` | the RA bits for short hop (6) and landing lag (30) |
| `src/main/game/techniques.cpp`, `src/extra/drill_ledge_core.h` | `source/pp/ledge_tech.cpp`, `source/pp/wavedash_info.cpp` | ledgedash tracking (frame types per frame from CliffWait to an actionable landing, GALINT from the body's intangibility timer counting on below zero, the later drop-off frame); wavedash timing from the jump squat, jump, airdodge and special landing statuses |
| `src/main/ui/ledgedash_hud.cpp`, `src/main/ui/ledgedash_hud.h` | `source/pp/ledge_tech.cpp` | the ledgedash frame bar: one cell per frame, the frame-type categories (ledge, falling, jumping, airdodge, landing, attack, GALINT, other), and the GALINT, frames-on-ledge and angle readouts (drawn with a neutral high-contrast palette) |
| `src/main/ui/pages/ledgedash_hud.cpp` | `source/pp/ui.cpp`, `source/pp/global_settings.cpp` | the Max Frames setting: range 30 to 64, default 50 |
| `src/main/game/overlays.cpp` | `source/pp/color_overlay.cpp`, `include/pp/color_overlay.h`, `source/pp/invis_techs.cpp` | colouring a fighter through its colour-blend module's sub colour; the tech and get-up subactions and the frame window hidden by the invisible-tech trainer, and hiding the overhead tag with it from `Fighter::processFixCamera` (its code patches are not used) |
| `src/main/ui/pages/color_overlays.cpp` | `source/pp/color_overlay.cpp` | the ProjectPunch overlay preset: its wait, dash, run and actionable groups |
| `src/main/ui/info_display.cpp`, `src/main/ui/info_display.h`, `src/main/ui/pages/info_display.cpp` | `source/pp/playerdata.cpp`, `source/pp/main.cpp`, `include/pp/anim_cmd_utils.h` | the Subaction and Cancel Flags rows and the Frame Data preset, after ProjectPunch's fighter-state text and its interrupt groups. The short group names are the mod's own |

ProjectPunch's own drawing, text-printing and vector files say they were
ported from other people's code. Nothing in this repository is taken from
those files.

```
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

## lil klon's dummy tools for ProjectPunch

- Source: a handoff package shared privately by lil klon (a patch against
  ProjectPunch `main` at `bf53bb3`, with his notes), with his permission to
  use and adapt it with credit.

No code is copied verbatim. These behaviors are adapted cleanly onto
Project+ 3.2.1 recording and CPU options:

| File | Upstream file | What |
|------|---------------|------|
| `src/main/record/start_input.h`, `src/main/record/record.cpp`, `src/main/record/hud.cpp`, `src/main/ui/pages/recording.cpp` | `extension/source/recorder.cpp` | Start On First Input recording arming: wait for the first non-neutral input before capturing frames, showing an Armed indicator until capture begins |
| `src/main/cpu/ledge_logic.h`, `src/main/cpu/options.cpp` | `extension/source/recorder.cpp`, `extension/source/actions.cpp` | On-Ledge reversal trigger on `Cliff_Wait` (`0x75`), respecting delay frames and context-aware ledge option inputs |
| `src/extra/reversal_slots_core.h`, `src/extra/reversal_slots.cpp`, `src/main/ui/pages/reversal_slots.cpp` | `extension/source/actions.cpp` | Weighted multi-slot reversal picking and per-slot delays across On-Hit, On-Shield, On-Getup, and On-Ledge triggers |

## fudgepop01 (PPlusAI and SuperTraining)

- Upstreams:
  - https://github.com/fudgepop01/PPlusAI (44-fighter `Chars/*/ai_*/103_recoveryHub.as`, `Include/shared/templates/103_recoveryHub.as`, `Include/shared/snippets/103_recoveryHub.as`)
  - https://github.com/Fracture17/ProjectMCodes (`Codes/SuperTraining/` only, sole-authored by fudgepop01; no Fracture17 `Libraries/`, `BuildSystem/`, or `menu.cpp`, and no third-party Eon/Bero snippets such as lines 33-142 of `hitboxHeatmap.cpp` are used)
- Licence: no licence file is published upstream; used and adapted with express
  permission from fudgepop01 with credit.

No code is copied verbatim. These algorithms and per-fighter recovery tables
are cleanly re-implemented in C++ against Project+ 3.2.1 runtime interfaces:

| File | Upstream file | What |
|------|---------------|------|
| `src/extra/pplus_recovery_core.h`, `src/extra/pplus_recovery_core.cpp`, `src/extra/pplus_recovery.h`, `src/extra/pplus_recovery.cpp` | `PPlusAI` `103_recoveryHub.as`, `Codes/SuperTraining/WeightedDie.*` | 44-fighter recovery geometry table across all 8 recovery families, stack-only `WeightedDie`, high-recovery mixup roll, 360-aim notch clamp, glide/float/tether/charged/PK Thunder orbit recovery logic |
| `src/extra/combo_trainer_core.h`, `src/extra/combo_trainer.h`, `src/extra/combo_trainer.cpp`, `src/main/ui/pages/combo_trainer.cpp` | `Codes/SuperTraining/main.cpp`, `Codes/SuperTraining/FudgeMenu.h` | Combo Trainer hitstun-drop auto-reset, `comboTimerAdjustment`, `noclip` reset (with explicit 1-frame `Correct_Air` restore), `recoveryTrainer` offstage loop, and 7 floating world-space state meters |
| `src/extra/hitbox_heatmap_core.h`, `src/extra/hitbox_heatmap.h`, `src/extra/hitbox_heatmap.cpp`, `src/main/ui/pages/display_options.cpp` | `Codes/SuperTraining/hitboxHeatmap.cpp` | fixed-ring hitbox trail accumulation, actionable-ground frame-age color progression, opacity/bounds controls, and Z-plane reach bounds; rendering calls stock Project+ 3.2.1's `soCollision::getShape` and debug bubble routine directly |
| `src/extra/adaptive_cpu_core.h`, `src/extra/adaptive_cpu.h`, `src/extra/adaptive_cpu.cpp` | `Codes/SuperTraining/MovementTracker.h`, `Codes/SuperTraining/MovementTracker.cpp`, `Codes/SuperTraining/PatternManager.h` | 128-entry static circular `MovementTracker` with similarity-weighted Markov probabilities (`approxChance`, `approxCommit`) and 15-entry weighted `PatternManager` RPS habit tracker (with slot-14 wrap and latest-entry start fixes) driving adaptive CPU counter biasing |

## Brawl Online

- Upstream: https://github.com/sarahkittyy/brawlonline.net (https://brawlonline.net)
- Commit read: af57a23ad3afbc7c04e8421ba906704a0aa105c5
- Licences: the repository's `NOTICE` gives one per folder. `dolphin/` is
  GPL-2.0-or-later, `launcher/` is GPL-3.0, `game-code/` is MIT outside
  `PPOnline/` ("Copyright (c) 2023 Sammi Husky", the MIT text above), and
  `server/` is GPL-2.0-or-later. No licence file or statement was found for
  `harness/`, `tools/` or `docs/`.

Used with the permission of Brawl Online's maintainer. No code is copied.
These are taken from it:

| File | Upstream file | What |
|------|---------------|------|
| `src/main/savestate/snapshot.cpp` | `dolphin/Data/Sys/Rollback/gp-v*.json` (GPL-2.0-or-later) | the list of fixed memory ranges a savestate copies (every address but one is in its lists, with the same length but for one, and with shortened labels) and the four excluded ranges |
| `scripts/nav.py` | `harness/ppharness/brawl.py` (no licence found) | the character-select and stage-select offsets and their comments, the player-state values, and the idea of steering a menu cursor with the stick while reading its position (the steering rule itself is the mod's own since it was rewritten). Brawl Online's file credits several of these to Orca |
| `scripts/fatimage.py` | `tools/sdcard/fat32.py` (no licence found) | written separately, but shaped like Brawl Online's FAT32 writer: the same helper names and parameter order, the same constants |
| `src/stub/inline_stub.cpp` | `game-code/PPOnlineLoader/source/loader.cpp` (MIT), `docs/game-code.md` | method only: the 3.2.1 `sy_core` inline-hook stub restores r0 and LR in the wrong order, and swapping two words repairs it |
| `src/stub/loader.cpp`, `src/stub/packed_file.cpp`, `scripts/pack-rel.py` | `game-code/PPOnlineLoader/` (MIT) | method only: a small plugin with further modules appended to its file, read whole and linked into another heap |

## Brawlback

Brawl Online (above) is built on two Brawlback projects, and what the mod
takes from Brawl Online's `dolphin/` and `game-code/` folders comes under
their licences. Brawl Online's `NOTICE` and the notices inside those two
folders are the source for this section.

- Project-Plus-Dolphin: https://github.com/Brawlback-Team/Project-Plus-Dolphin
  - Licence: GPL-2.0-or-later, as Dolphin itself.
  - Brawl Online's `dolphin/` is a fork of it (`dolphin/NOTICE-BRAWLONLINE.txt`,
    which names the Brawlback team: WhiteTPoison, PiNE (FaultyPine), Kapedani,
    eliasreid, rapito, Believer1 and others).
  - What the mod has from that tree: the fixed memory ranges in
    `src/main/savestate/snapshot.cpp`, listed in the Brawl Online table above
    (`dolphin/Data/Sys/Rollback/gp-v*.json`). The emulator itself is only what
    the tests run on; it is not part of the mod or its packages.
- brawlback-asm: https://github.com/Brawlback-Team/brawlback-asm
  - Licence: MIT, with the copyright line "Copyright (c) 2023 Sammi Husky"
    (the licence's wording is the MIT text printed in the SyriingePluginTemplate
    section above).
  - Brawl Online's `game-code/` is a fork of it (`game-code/NOTICE`). The mod
    copies no code from it; the loader's method, listed in the Brawl Online
    table above, was read in Brawl Online's own `PPOnlineLoader/` there.

## Project+

- Upstream: https://projectplusgame.com, by the Project+ Development Team
- Version: 3.2.1
- Licence: no licence file or statement was found in the Project+ build.

Nothing from Project+ is included in this repository. The mod calls
Project+'s own code inside the game; [CREDITS.md](CREDITS.md) lists which
codes and their authors. One routine is re-implemented:
`src/main/display/debug_draw.cpp` repeats the list walk of Project+'s stage
collision display ("renderDebug/[grCollisionManager]" by Eon, in
`Source/Project+/Debug/Stage Collisions.asm`) and then calls Project+'s own
drawing routine for each entry.

## Submodules

These are referenced as git submodules, not copied:

- `lib/BrawlHeaders`: https://github.com/Sammi-Husky/BrawlHeaders at
  3ce22366a70c126692e8ad2a252d90918858982c, whose headers are a mirror of the
  doldecomp/brawl decompilation's `include/` (identical at that commit;
  OpenRVL is a plain folder there, no longer a nested submodule). No licence
  file is published upstream. Used at build time only: its headers are
  included and its `RSBE01.lst` symbol list is passed to elf2rel. None of its
  source code is compiled into the plugin.

## doldecomp/brawl

- Upstream: https://github.com/doldecomp/brawl
- Licence: CC0-1.0 (public domain dedication)

Not included. The addresses of the symbols added in `link/EXTRA.lst` were
taken from its RSBE01 rev 2 symbol maps (`config/RSBE01_02/symbols.txt` and
`config/RSBE01_02/rels/sora_melee/symbols.txt`): `OSLinkFixed`,
`gfTaskScheduler::setPauseCategory`, `soAnimCmdEventObserver::addObserver`,
`OSDisableInterrupts`, `OSRestoreInterrupts`, `GXDrawDone`,
`GXInvalidateVtxCache`, `GXInvalidateTexAll`, `OSSleepTicks`,
`OSTicksToCalendarTime`, `GXLoadPosMtxImm`, `ftCommonDataAccesser::getData`,
`g_ftCommonDataAccesser`, `ipPadConfig::getInstance`, `GXInitTexObjCI`,
`GXInitTexObjLOD`, `GXLoadTexObj`, `GXInitTlutObj`, `GXLoadTlut`,
`GXSetZCompLoc`, `GXSetTevSwapMode`, and `GXSetTevSwapModeTable`. Two more,
`gfFileIO::checkSDInserted` and `gfFileIO::checkSDWriteProtected`, are unnamed
there and were named here from their code.
