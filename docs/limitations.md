# Known Limitations

This page documents what Project+ Training Mode Mod (`P+TMM`) cannot do, what is supported only in part, and what has not been exhaustively tested yet.

## Platform and Compatibility

- **Dolphin primary; Wii hardware testing status.** Automated testing runs in Dolphin, including a strict Wii-compatibility gate (`make test-wii-strict` with MMU enabled, Fastmem disabled, non-JIT CPU core, DSP LLE, and 4-player MEM2 heap checks) alongside real-Wii safety measures (cache-tiled savestate restores with periodic IRQ yields and `OSDisableScheduler`, plus `Heaps::Network`-allocated async SD DMA buffers). Earlier pre-releases booted, ran savestates, recorded inputs, and saved to SD on real Wii hardware, but the newest `pre4` features and frame times have not yet been thoroughly benchmarked on physical Wii hardware.
- **Offline only.** `P+TMM` modifies frame-by-frame gameplay execution and must not be loaded for netplay. As a safety guard, the stub refuses to load the match or extra modules whenever Project+ Netplay (`NETBOOST.GCT` or the Netplay VSync patch) or Replay playback is active.
- **Not alongside ProjectPunch.** Project+ does not have enough free memory for both training plugins to run simultaneously. `P+TMM` folds in ProjectPunch's core training tools (with Wisp's blessing) so you only need one training plugin.
- **Project+ v3.2.1 only.** The mod hooks and reads fixed addresses in NTSC-U Super Smash Bros. Brawl (`RSBE01` rev 2) running Project+ v3.2.1. Other Project+ versions or Brawl mods require their own build.

## Training and Offline Versus Matches

- **No Final Smashes.** Project+ declines to grant Final Smashes while certain memory regions are in use, and `P+TMM` uses one of them during Training.
- **No Assist Trophies in Training.** Assist Trophy preloading is disabled in Training while `P+TMM` is loaded because `AssistFigureResource` holds the mod's `extra` module (leaving all four fighter heap sets free for 4-player Training, Code Menu character changes, and Fighter Swap), and Mr. Resetti would otherwise claim `FighterTechqniq` (used for input recording). In opted-in local offline Versus with Assist Trophies turned on, `P+TMM` leaves `AssistFigureResource` free for Brawl and loads the `main` match module only.
- **Items are lightly tested.** Aside from Assist Trophies in Training, items are not blocked, but automated tests cover only character-owned items (turnips, bombs, grenades, bananas, and similar).

## Stage Options

- **Dynamic stages only.** Live **Stage Options** (under **Stage & Cast**) appear automatically on stages with supported live hazard or moving-platform controls: Fountain of Dreams, Dream Land, Smashville, Green Hill Zone, Dead Line, Luigi's Mansion, Delfino's Secret, Yoshi's Story, Yoshi's Island (Brawl), and other stages with animated moving collision platforms. On static stages such as Final Destination, Stage Options rows stay hidden unless **Show Inactive Fighters** is toggled on.

## Savestates

- **Two players at most.** Savestates work in 1v1 Training matches (Ice Climbers count as one player, so Ice Climbers vs. Ice Climbers is supported). Saving is refused when more than two fighter entries are active.
- **Zelda and Sheik cannot save after transforming.** Once Zelda transforms into Sheik (or Sheik into Zelda) during a match, saving is refused while the transformed fighter is active, and any state saved before the transformation cannot be loaded again (even after transforming back). This applies to both HMN and CPU. Selecting Sheik directly on the character select screen works normally.
- **States belong to the current match.** Savestates live in RAM, cannot carry across matches, and are not written to the SD card. Loading is refused if the game has loaded or freed an archive file since the save, because restored pointers would no longer match.
- **Savestate feedback.** Holding D-Pad Right saves once with a confirm sound and a brief tinted flash (flash omitted during Frame Advance). Loading plays the back sound. A refused save or load plays the error sound, and structural refusals (more than one CPU, transformed fighter, no room beside a recording's state, or a file loaded since the save) also post a red OSD message.
- **Audio is not saved.** Music and sound effects continue from their live playback positions when a state is loaded.

## Recording

- **One minute per slot.** Each of the 6 HMN slots and 6 CPU slots holds up to 3,600 frames.
- **Session lifetime unless exported.** Recordings clear when the match ends unless you export them to `.tmr` files on SD.
- **Imported recordings are placement starts, not full savestates.** Importing restores both fighters' positions, facing, percent, and shield, and replays recorded inputs deterministically from that start. It does not restore the original match's RNG state, stale-move queues, items, projectiles, or fighter charges; a fighter exported in the air starts in the air and falls from there.

## DI Display

The predicted trajectory uses Project+'s own knockback, gravity, ASDI, and DI equations and matched real free-flight motion to within a thousandth of a unit in testing. Known approximations:

- **Landing marks can be off by a frame.** Exact foot height depends on the active animation, which the predictor does not play back.
- **Grazed ceilings can be missed.**
- **Post-contact dashed lines assume no tech.** Floor slides and bounces off upright walls or level ceilings are traced; slopes and corners stop the line because their bounce depends on details the predictor does not model. In testing, floor slides ended within one unit and ceiling bounces within two units of real motion.
- **Not modeled:** Wind, moving platforms, and follow-up hits.
- **CPU Random DI:** When a CPU uses Project+'s built-in Random DI, only the no-DI path is drawn because the angle is not chosen until launch.
- **Project+ Draw DI:** Project+'s built-in Draw DI is switched off while `P+TMM`'s DI Display is active so the two do not conflict.

## Stage & Cast (Fighter RNG and Mechanics)

- **18 supported fighters.** Under **Stage & Cast**, rows for Peach, Luigi, Mr. Game & Watch, Samus, Mewtwo, Lucario, Wario, Donkey Kong, Ivysaur, R.O.B., Lucas, Squirtle, Sheik, King Dedede, Snake, Ganondorf, Ice Climbers (Nana Throw), plus the dedicated **Olimar** submenu appear automatically when that fighter is in the match. Toggle **Show Inactive Fighters** to browse all rows regardless of the current matchup.
- **Luigi's Misfire.** Project+ tracks a counter across Side Specials: each non-misfire increments it, the seventh misfires, and standing still or misfiring re-rolls the count. `Always` and `Never` position the counter around that threshold; `Default` leaves it alone. Storing a misfire while charging still fires a normal Side Special on that release under `Always`.
- **Mr. Game & Watch's Judge.** Project+ pre-rolls the next Judge number during the previous Judge and never repeats either of the last two numbers. Returning to `Default` leaves one queued number before normal rolls resume.
- **Peach's Forward Smash.** Project+ cycles Golf Club, Frying Pan, and Tennis Racket deterministically rather than randomly. Returning to `Default` continues the cycle from the weapon you last forced.
- **Multi-CPU RNG:** Character RNG was verified in 1v1 matches (player and CPU) across emulator savestates, lab savestates, and recording playback, not with multiple simultaneous CPUs.

## Menu Sounds

- **Verified via sound-request instrumentation.** Automated tests run muted and verify that each menu press requests the expected Brawl menu sound ID and that the game accepts the request.

## CPU Options

- **Invisible tech animations use a fixed window.** Ground techs use Project+'s per-character first distinguishable frame; get-up and slip get-up windows are adapted from ProjectPunch.
- **Perfect Shield timing limits:**
  - Cannot react to true frame-1 hitboxes (such as Shine or Rest), which hit on the same frame the move begins.
  - Misses point-blank attacks whose hitbox first appears on the exact hit frame (such as a close-range jab), because it inspects already-active hitboxes.
  - Can guard a sharply bouncing or arcing projectile as a normal shield rather than a powershield, because projectile prediction extrapolates one frame of motion linearly.

## Testing Scope

Most automated gameplay tests run Mario and Fox on Final Destination, with Battlefield for platforms. Because `P+TMM` reads parameters and collision data live from the game for whichever fighter and stage are loaded, other fighters and stages follow the same rules, though not every combination has an individual test case.

Savestates have the broadest roster coverage: every fighter was tested as the player on Final Destination across aerials, specials, items, projectiles, charges, and partners (including Nana, Pikmin, grenades, bombs, boomerangs, arrows, turnips, bananas, and Kirby copy abilities), plus 30 fighters as the CPU. Multi-stage savestate determinism was checked across Battlefield, Dream Land, Pokemon Stadium 2, Yoshi's Story, and seven additional dynamic/hazard stages (on Smashville, the Town Hall clock reads live system time after a load).

Not yet verified in automated tests: Nana acting independently of Popo, Brawl's native AI driving the CPU alongside all lab tools, and non-character stage/crate items.
