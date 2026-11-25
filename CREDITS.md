# Credits

Training Mode for Project+ stands on a lot of community work. Some of it is
ProjectPunch's MIT-licensed training algorithms, some is code the mod calls
inside the game (Project+'s own debug drawing and its Code Menu), and a lot of
it is knowledge from the Brawl decompilation and years of community research.
This page lists every project the mod draws on, what it uses, and how.

There are five ways the mod uses someone else's work, and each section says
which apply:

- **Copied**: text or code taken over, sometimes edited.
- **Adapted**: an algorithm, rule, layout or table written again here after
  reading their source.
- **Called in the game**: the mod runs or reads another project's code or data
  while you play.
- **Learned from**: documentation, symbol names or source read to understand
  the game.
- **Built or tested with**: tools used to build the mod or run its tests.

The exact file-by-file record of everything copied or adapted, with each
project's licence, is in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## ProjectPunch

[ProjectPunch](https://github.com/WispSSBM/ProjectPunch) by Wisp is the
earlier Project+ training mod. Its core training tools are folded into this
mod, because the two cannot be installed together.

- **Adapted**: frame advance and its hold delay; the way the game is stopped
  while frame advance holds it; fighter status and animation-command tracking;
  on-hit and on-shield frame advantage and its tolerance setting; ledgedash
  and wavedash tracking, GALINT, and the ledgedash frame bar; colour overlays
  and the ProjectPunch overlay preset; the invisible-tech trainer; the
  fighter-state rows of the info display.

ProjectPunch's invisible-tech trainer comes from ilikepizza107's "Partially
Invisible Techs" script (GameBanana script 12025).

ProjectPunch is MIT licensed.

## lil klon

lil klon, a ProjectPunch contributor, built recording, playback and reversal
tools for ProjectPunch's training dummy. He shared his source and his notes
with this project, with permission to use them, and has been a huge help
along the way. Thanks, klon!

- **Adapted**: the "Start On Input" recording option (holding capture until
  the first non-neutral controller input, with an Armed HUD state), written
  from his handoff's start-on-first-input recording design; and the On-Ledge
  reversal trigger design (triggering on actionable `Cliff_Wait` `0x75` frames
  while respecting counter delay and leaving `Cliff_Catch` startup untouched)
  in `src/main/cpu/ledge_logic.h` and `src/main/cpu/options.cpp`.
- **Learned from**: his source and his write-up of what he ran into while
  building dummy tools on Project+ 3.2.1 (how crouch-cancelled hits behave,
  how a dummy's forced inputs can leak into menus that read every controller,
  where Training's pause menu keeps its state), read beside this mod's own
  recording and counter-action code.

## fudgepop01 (PPlusAI and SuperTraining)

fudgepop01 authored [PPlusAI](https://github.com/fudgepop01/PPlusAI) and the
SuperTraining / FudgeMenu training suite inside
[ProjectMCodes](https://github.com/Fracture17/ProjectMCodes), and gave express
permission for this project to adapt those algorithms with credit. Thanks,
fudgepop01!

- **Adapted**: the 44-fighter Project+ recovery framework (`103_recoveryHub.as`
  per-fighter geometry, 8 recovery families, 360-aim dead-zone notch clamping,
  high-recovery mixups, `{GLIDE}` / `{BREVERSE}` / tether / float / PK Thunder
  orbit rules, and stack-only `WeightedDie`) re-implemented in pure C++ in
  `src/extra/pplus_recovery*`; the Combo Trainer backend (`noclip` auto-reset,
  `comboTimerAdjustment`, random position/damage resets, `recoveryTrainer` loop,
  and 7 floating world-space state meters) adapted onto Project+ 3.2.1 APIs in
  `src/extra/combo_trainer*`; the Hitbox Heatmap trail ring-buffer
  accumulation, actionable-ground frame-age color bands, opacity/bounds
  controls, and live reach-bounds design adapted in `src/extra/hitbox_heatmap*`
  (rendering through stock Project+ 3.2.1's `soCollision::getShape` and debug
  bubble routines, with no Eon/Fracture17 code copied); and the Adaptive CPU
  backend (`MovementTracker` 128-entry similarity-weighted Markov action
  tracker and `PatternManager` 15-entry weighted RPS habit tracker, with
  wrap-around and start-index fixes) re-implemented in pure C++ in
  `src/extra/adaptive_cpu*`.
- **Learned from**: SuperTraining's frame-data animation-command thread
  structures, read while building `src/extra/frame_data.cpp` from the game's
  own code.

## Project+

[Project+](https://projectplusgame.com) by the Project+ Development Team is
the game this mod runs in, and a lot of the mod works by using what Project+
already does. Project+ itself builds on the work of the Project M Development
Team and the Legacy TE team. The authors below are named the way each code's
title in the Project+ source names them.

**Called in the game.** The hitbox, hurtbox, shield, ECB and stage collision
displays don't draw anything themselves: they call Project+'s own debug
drawing routines, so what you see is exactly what the game uses.

- Hitbox and hurtbox display, stage collision display, the debug colour file
  and how the debug drawing is called: Eon.
- ECB drawing: Arthur and Eon.
- The Code Menu, originally by Fracture for Legacy TE and maintained for
  Project+ by QuickLava: the DI Display reads its SDI and ASDI distance
  settings, and the mod's drawing hooks in next to it.
- The Syriinge loader Project+ ships, which loads this mod: Sammi Husky and
  DukeItOut.
- The SD card file patch that lets the mod read and write its `tm` folder:
  Sammi Husky.
- The extra fighter information Project+ keeps, which the info display's ECB
  Lock value reads (the rule that keeps the bottom of the collision shape at
  the feet for the first frames in the air): Magus and DukeItOut.

**Rules the mod follows or simulates.** The DI Display, the CPU options, and
the onscreen displays model Project+'s rules directly, and those rules come
from these codes:

- Directional influence (trajectory DI, the stick scaling, ASDI with the
  C-stick, ASDI downwards on the ground, DI below tumble): Magus, DukeItOut and
  Dantarion.
- The stick and trigger scales the info display shows: the stick cap and
  multiplier ("Directional Input Capped at Distance of 1", "General
  Directional Input Multiplier"): Magus; the analogue C-stick and trigger
  variables ("Analog C-Stick, L R, & Light-Shield Button Stored as
  Variables"): Magus and DukeItOut.
- L-cancelling: Eon, with the landing check by Magus, Standardtoaster,
  wiiztec, Eon, DukeItOut and mawwwk.
- Powershielding: Shanus and Magus. The powershield's reflection of
  projectiles, which the CPU's Powershield behavior uses: Magus and DukeItOut.
- Hitstun and tumble rules: Shanus, with DukeItOut and Eon.
- Teching: the tech window fixes and the tech in hitlag against a wall or a
  ceiling ("edgeteching"): Magus.
- Instant fastfalls: Fudgepop01 and DukeItOut.
- Airdodges and wavedash landings: Eon.
- Jump-cancelled grabs: Shanus and Magus.
- Slow turns into dashes: Magus.
- Grab release rules: Phantom Wings and Magus.
- Move staling in Training: Eon, and the stale move ratios: spunit262.
- Stage ray checks that ignore collision lines with no side, which the DI
  Display relies on to find where a path meets the stage: Eon.
- The hitstun multiplier: Phantom Wings and spunit262.
- Training's CPU options and random DI: Eon, PyotrLuzhin and QuickLava.
- Debug pause and frame controls, which the mod works around while it holds
  the game: Magus and Eon (with help from fudgepop01), and an author the code
  leaves unnamed.
- Independent button presses and the analog inputs stored for fighters, which
  recordings read: Magus, Eon and DukeItOut.
- Boot to the character select with Training on shield: PyotrLuzhin,
  SammiHusky and QuickLava.
- Alternate stages, the stage file system and the camera: DukeItOut and
  Kapedani. Replays: Fracture, Kapedani and DukeItOut.
- Hold Start to pause: standardtoaster and DukeItOut. The LA variable
  expansion that savestates copy: Magus and DukeItOut.
- Music selection, which recordings remember: Squidgy and mawwwk.
- The item clone engine the tests read: Sammi Husky and Kapedani.
- Peach's forward smash, whose three weapons come in turn in Project+, and
  her turnip faces and item pulls with their chances; Luigi's misfire count;
  and Mr. Game & Watch's two-number Judge history, which the Character RNG
  page reads and sets: Project+'s fighter scripts, modules and parameters
  (which carry no author names).

Project+ doesn't publish a licence. Nothing from Project+ is included in this
repository; you need your own Project+ build.

## Syriinge and the plugin template

[Syriinge](https://github.com/Sammi-Husky/Syriinge) by Sammi Husky is the
plugin loader that runs this mod inside Brawl, and the
[SyriingePluginTemplate](https://github.com/Sammi-Husky/SyriingePluginTemplate)
is what the build started from.

- **Copied**: the Syriinge SDK headers and the template's linker script.
- **Adapted**: the template's Makefile, symbol list and plugin entry points.
- **Called in the game**: Syriinge loads the mod and installs its hooks.
- **Built with**: the template's copy of the compiler and
  [elf2rel](https://github.com/Sammi-Husky/elf2rel), which `make tools`
  downloads.

Both are MIT licensed.

## The Brawl decompilation and BrawlHeaders

The [Brawl decompilation](https://github.com/doldecomp/brawl) and
[BrawlHeaders](https://github.com/Sammi-Husky/BrawlHeaders), now a mirror of
the decompilation's headers, are how the mod knows the game's classes,
functions and addresses. Thanks to everyone who has contributed to them.

- **Learned from**: the decompilation's symbol maps and source, read to find
  how the game works and where things are.
- **Built with**: the mod is compiled against BrawlHeaders and linked with its
  symbol list, plus a few addresses from the decompilation's symbol maps.

The decompilation is released under CC0. BrawlHeaders doesn't publish a
licence file.

## Brawl Online

[Brawl Online](https://brawlonline.net) ([source](https://github.com/sarahkittyy/brawlonline.net))
is the rollback netplay build for Project+, itself built on Brawlback (see
below). Its Dolphin build is what the mod is developed and tested on.

- **Built or tested with**: the tests run on Brawl Online's Dolphin build and
  drive it through its automation harness, following the harness's documented
  protocol.
- **Adapted**: the list of memory ranges a savestate copies comes from Brawl
  Online's rollback range lists. The test scripts' character-select and
  stage-select offsets and the idea of steering a menu cursor come from its harness
  library, and the test SD card writer is modelled on its FAT32 tool.
- **Learned from**: the fix for the broken inline-hook stub in Project+
  3.2.1's Syriinge, and the idea of a small plugin that loads larger modules
  into another heap, both from its PPOnline loader; and its documentation of
  game addresses.

Brawl Online's material is used with the OK of its maintainer, sarahkittyy.
Thanks!

Brawl Online's folders carry different licences (GPL for its Dolphin and
launcher, MIT for its game code); its harness, tools and documentation don't
publish one.

## Brawlback

[Brawlback](https://github.com/Brawlback-Team) is the rollback netcode project
for Brawl that Brawl Online stands on, so a good part of what we got from
Brawl Online goes back to the Brawlback team: WhiteTPoison, PiNE (FaultyPine),
Kapedani, eliasreid, rapito, Believer1 and the other contributors in its
history.

- **Built or tested with**: the Dolphin the tests run on is Brawl Online's
  fork of Brawlback's
  [Project-Plus-Dolphin](https://github.com/Brawlback-Team/Project-Plus-Dolphin)
  (GPL-2.0-or-later), which is itself built on Dolphin. The savestate's list
  of memory ranges comes from the rollback range lists that ship with that
  Dolphin.
- **Learned from**: Brawl Online's game code, where we read how a small
  Syriinge plugin loads larger modules, is a fork of
  [brawlback-asm](https://github.com/Brawlback-Team/brawlback-asm) (MIT).

Nothing of Brawlback's is copied into the mod or shipped with it.

## Other references

- [ProjectMCodes](https://github.com/Fracture17/ProjectMCodes) by Fracture17:
  its Brawl function-name map was **learned from** while researching how the
  game handles DI, menus and CPU players. Its SuperTraining code, by
  fudgepop01 (used with permission), is credited in the `fudgepop01` section
  above.
- [BrawlRE](https://github.com/BrawlRE/BrawlRE.github.io)'s documentation of
  Brawl: its tables of Peach's turnip parameters (the one-in-N chance of an
  item and her item table) and of the item kinds were **learned from** while
  finding where Project+'s Peach keeps hers.
- [PowerPC-Assembly-Functions](https://github.com/QuickLava/PowerPC-Assembly-Functions)
  by QuickLava, the source that generates Project+'s Code Menu and its Draw DI:
  **learned from**, to understand the Code Menu's settings and to check the DI
  Display against Draw DI.

## Tools

- **Metrowerks CodeWarrior** 4.2 for the Wii, the compiler and linker
  Syriinge plugins are built with. It isn't part of this repository.
- [devkitPro](https://devkitpro.org)'s MSYS2, for `make` and a shell on Windows.
- [Dolphin](https://dolphin-emu.org), through Brawl Online's build, for running
  and testing the mod.
- [Python](https://www.python.org) for the build's packing step and the tests,
  and [Pillow](https://python-pillow.org) for some of the tests' screenshot
  checks.

## Missing or wrong credit?

If your work is used here and isn't credited, or is credited wrongly, please
open an issue on this repository or contact skier. It'll be fixed.
