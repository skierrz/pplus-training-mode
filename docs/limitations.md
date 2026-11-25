# Known limitations

This page lists what the mod can't do, what it only does in part, and what
hasn't been checked yet.

## Where it runs

- **Dolphin-first testing.** Automated testing runs in Dolphin. Earlier
  pre-releases booted, ran savestates, recorded inputs, and saved to SD on real
  Wii hardware, but this pre-release's newest features have not yet been
  benchmarked on console. Savestates copy about 5 MB of memory each way (a
  little more for Kirby), and real-hardware frame-time measurements remain to
  be collected.
- **Offline only.** The mod changes how the game runs frame by frame, so don't
  load it for netplay.
- **Not alongside ProjectPunch.** Project+ doesn't have the memory for both.
  This mod folds in ProjectPunch's core training tools.
- **Project+ v3.2.1 only.** The mod reads the game's memory at fixed places.
  Another version of Project+, or another Brawl mod, would need its own build.

## Training matches

- **No Final Smashes.** Project+ refuses to hand out a Final Smash when one of
  the game's memory areas is in use, and the mod keeps part of itself there
  during Training. Project+ isn't played with Final Smashes, so this is left
  as it is.
- **No Mr. Resetti.** Mr. Resetti's Assist Trophy does not appear in Training
  while the mod is loaded because his files would take the memory area the mod
  uses for input recording.
- **Items are barely tested.** The mod doesn't stop items working, but almost
  none of the tests use them.
- **Three different characters at most.** The Code Menu's Character Select
  (when **Controls > P+ Code Menu Shortcut** is turned on in the lab) can give
  each CPU its own character, but the game's memory for a fourth different
  character (`OverlayFighter4`) is where the mod keeps its extra module, and
  the match would freeze. A change that would bring in a fourth character is
  refused and that CPU stays on the CPU's character.

## Stage Options

- **Dynamic stages only.** Live **Stage Options** (under **Stage & Cast**)
  enable automatically on stages with live hazard or moving-platform controls
  (such as Fountain of Dreams, Smashville, Green Hill Zone, Dream Land, Luigi's
  Mansion, and Delfino's Secret). On static stages like Final Destination, the
  Stage Options rows are hidden unless **Show Inactive** is toggled on.

## Savestates

- **Two players at most.** Savestates work in a match with two players. Ice
  Climbers count as one player, so even Ice Climbers against Ice Climbers
  works. Saving is refused in a match with more than two players.
- **Zelda and Sheik can't save after changing.** Once Zelda has turned into
  Sheik, or Sheik into Zelda, in a match, saving is refused while the changed
  character is out. A state saved before the change can't be loaded again
  either, not even after changing back. This holds for you and for the CPU.
  Picking Sheik on the character select works normally.
- **A state belongs to the match it was saved in.** It can't be carried to
  another match, and it isn't written to the SD card. A load is also refused
  if the game has loaded or freed a file since the save, because the state
  would no longer fit.
- **Saving feedback.** Holding D-pad Right saves once (brief tinted screen
  flash + confirm sound; flash omitted in Frame Advance). Loading plays the
  back sound. A refused save or load plays the error sound, and structural
  refusals (more than one CPU, transformed fighter, no room beside a
  recording's state, file loaded since save) also post a red OSD message.
- **Sound isn't part of a state.** Music and sound effects carry on from
  wherever they were when you load.

## Recording

- **One minute per slot.** A slot holds up to 3,600 frames.
- **Recordings last for the match.** They're gone when the match ends unless
  you export them.
- **An imported recording isn't an exact copy of the original moment.** Import
  puts both fighters back at their saved positions with their percent, facing
  and shield, and playback is exact from there. It doesn't bring back the
  original match's random numbers, stale moves, items, projectiles or charges,
  and a fighter saved in the air starts standing in the air.

## DI Display

The line is worked out from Project+'s own rules, and in free flight it
matched where the fighter really went to within a thousandth of a unit in
testing. Past that, a few things are approximate or left out:

- **The landing mark can be a frame early or late.** Where your feet are
  depends on your animation, and the line doesn't follow the animation.
- **A ceiling you only graze can be missed.**
- **The dashed line assumes you don't tech.** It follows a slide along the
  floor and a bounce off an upright wall or a level ceiling, and it stops at
  slopes and corners, where the bounce depends on details the line doesn't
  track. In testing a slide ended within a unit of the real one and a ceiling
  bounce stayed within two units.
- **Not predicted:** wind, moving platforms, and anything that hits you later.
- **For a CPU using Project+'s Random DI**, only the no-DI path is drawn,
  since the direction isn't known until the game picks it.
- **Project+'s own Draw DI is switched off** while the DI Display is on,
  because the two interfere with each other.

## Stage & Cast (Fighter RNG & Mechanics)

- **17 supported fighters.** Under **Stage & Cast**, rows for Peach, Luigi,
  Mr. Game & Watch, Samus, Mewtwo, Lucario, Wario, Donkey Kong, Ivysaur,
  R.O.B., Lucas, Squirtle, Sheik, King Dedede, Snake, Ganondorf, and Ice
  Climbers (Nana Throw) appear automatically when that fighter is in the match
  (toggle **Show Inactive** to browse all rows).
- **Luigi's misfire isn't one roll per side special in Project+.** A count
  decides it: each side special that doesn't misfire counts up, and the
  seventh misfires. Standing still draws the count again at random every
  frame, and so does a misfire. Always and Never move the count to one side of
  the seventh; Default leaves it alone. If you store the misfire while
  charging, that side special stays a normal one under Always.
- **Mr. Game & Watch's Judge looks one number ahead in Project+.** Each Judge
  shows the number drawn at the one before, and that draw never repeats either
  of the last two. When you go back to Default, the next Judge still shows the
  number you had chosen; the draws are random again after that.
- **Peach's forward smash isn't random in Project+.** The golf club, the
  frying pan and the tennis racket come in turn, and Default leaves that
  alone. When you go back to Default, the turn carries on from the weapon you
  had chosen.
- **Checked with one Peach as the player and one as the CPU,** using the
  emulator's savestates, the lab's savestate and a recording's playback. Not
  yet checked: more than one CPU.

## Menu sounds

- **Checked by what the game is asked to play, not by ear.** The tests run
  without sound. They check that every press asks the game for the right one
  of its menu sounds and that the game accepts it.

## CPU options

- **Invisible tech animations use a fixed window** (get-up window adapted from
  ProjectPunch, ground techs using P+'s per-character distinguishable frame).
- **Perfect Shield misses point-blank attacks whose hitbox first appears on
  the hit frame** (such as a close-range jab), because it only checks hitboxes
  that are already active.
- **Perfect Shield cannot react to true frame-1 hitboxes** (such as Shine or
  Rest), which land on the same frame the move begins with no earlier frame to
  predict them.
- **Perfect Shield can guard a sharply bouncing or arcing projectile as an
  ordinary shield rather than a powershield**, because the prediction extends
  its last frame of motion in a straight line.

## What has been tested

Most tests use Mario and Fox on Final Destination, with Battlefield for
platforms. The mod reads its rules from the game for whichever character is on
screen, so other characters and stages should behave, but they haven't each
been checked.

Savestates have had the widest check. Every character was tried as your
character on Final Destination: aerials, specials at several delays, thrown
items and full charges, with projectiles, items and partners out (Nana,
Pikmin, grenades, bombs, boomerangs, arrows, turnips, bananas and more, and
Kirby with a copied ability). After each load the game played on exactly as
the first time, apart from Zelda and Sheik changing (above). Thirty characters
were also tried as the CPU.

Other stages: Battlefield, Dream Land, Pokemon Stadium 2 and Yoshi's Story
matched exactly with Mario, Ice Climbers and Link (and one run with Peach on
three of them). Seven more stages picked for moving parts or hazards got two
quick checks each with Mario: five matched exactly, and two differed only in
the stage's own sound, which isn't part of a state. On Smashville the Town
Hall clock in the background shows the real time, so after a load it shows the
current time and not the time of the save; nothing else on the stage
differed.

Not checked at all: Nana apart from Popo, the game's own AI driving the CPU,
and items that aren't a character's own.
