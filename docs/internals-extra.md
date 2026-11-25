# Internals of the extra module

How the extra module's services (`src/extra/`) interact with Brawl and
Project+ in more detail than the source comments: what each one is built on
and where it was read or measured. Statements marked "measured" were measured
in the emulator on RSBE01 rev 2 with Project+ 3.2.1 (with the character and
stage where recorded); "unchecked", "not checked" and "assumed" mark what was
not.

## DI Display

`src/extra/di_display.cpp`

How a hit becomes movement on RSBE01 rev 2 with Project+ 3.2.1, measured frame
by frame in the emulator with inputs played on exact frames (`scripts/di_tests.py --measure`;
Mario's smashes and up tilt on Fox):

- The kinetic module (`soModuleEnumeration +0x7C`) lists its energies from
  `+0x4C`, 12 bytes each: the pointer, then the energy's id. Id 1 (`+0x58`) is
  gravity: speed x, y at `+8`/`+0xC`, the fall acceleration at `+0x10`
  (negative; -0.23 for Fox) and the largest fall speed at `+0x14` (2.8). Id 4
  (`+0x7C`) is the knockback, speed at `+8`/`+0xC`. During hitlag it holds the
  launch speed before DI and the gravity speed is 0.
- Trajectory DI is applied on the first frame after hitlag, from that frame's
  stick: the launch turns by 18 degrees times `c` squared, keeping `c`'s sign,
  where `c` (clamped to `-1..1`) is the stick's component across the launch,
  positive counter-clockwise. The speed keeps its length. Project+ first scales
  the stick back onto a circle of 1.0025 (`Source/ProjectM/DirectionalInfluence.asm:25-88`).
- Automatic smash DI (ASDI) moves the fighter on that frame by the ASDI
  distance times the hit's smash DI multiplier (damage log `+0x30`) times the
  last hitlag frame's stick (scaled onto a circle of 1), if that stick's length
  is at least the ASDI threshold. Down on the ground, the fighter stays there
  (it lands or is knocked down).
- Smash DI moves the fighter by the SDI distance times the stick on a hitlag
  frame where the stick is pushed past the same threshold into a new direction
  (from the centre, or another of 8 directions); never down through the floor.
- Then every frame, the launch frame included: the knockback speed loses the
  decay from its length, the gravity speed gains the fall acceleration down to
  the largest fall speed, and the position moves by both.
- The threshold (`0.7`) is common parameter 3173 at `0x80B88350`. The SDI
  distance (`6`), ASDI distance (`3`) and decay (`0.051`) are Code Menu values
  Project+ writes every frame into `sora_melee`'s data at `0x80B88354`,
  `0x80B88358` and `0x80B88534` (`Source/CodeMenu/CodeMenu.asm:2218-2251`,
  hook at `0x80023D60`, which also writes 3285 at `0x80B88510`). Each is
  range-checked before use.
- Project+'s Random DI replaces the stick of a fighter whose AI flag is set
  (`cpu/fighter_input.h`): for those only the path without DI is drawn.

The stage (`scripts/di_tests.py --measure-ray`, Final Destination and
Battlefield): the path stops where the fighter first meets it.

- The game's `stRayCheck(pos, dir, hit, normal, b1, collision, b2, mask)`
  (`0x809326D8`, `sora_melee +0x227CC4`; read in a memory dump, then measured)
  builds a segment from `pos` to `pos + dir` in x and y and walks the stage's
  collision list (`0x8049E57C`), skipping no-side lines (Project+'s patch at
  `0x80138154`). One call costs about 0.6 microseconds when it meets nothing
  and 3 to 4.5 microseconds when it does.
- A fighter lands when its ECB's bottom point crosses a floor; the landing
  shows on the frame whose move crossed it, at that frame's x, the feet put on
  the floor. Because the ECB rises in tumble (3.5 to 6.7 units for Fox), an
  ECB with its bottom at the feet is taken to rise to 0.4 of its height.
- Each move is tested with one ray from the ECB's bottom (moving down) or top
  (moving up) and one from the side it moves to. Coarse 4-frame chords are
  refined frame by frame only on contact. Drawing one fighter's paths takes
  0.1 to 0.3 milliseconds.

After the first contact the line continues, dashed and thinner, assuming no
tech (`scripts/di_tests.py --measure-bounce`):

- Landing in tumble: `DamageFly` or `DamageFall` becomes `Down` (`0x4A`), and
  a grounded hit with downward ASDI stays grounded unless its first frame rises
  more than ASDI moved it down. Horizontal knockback speed loses `Ground_Brake`
  (param 3004) times `Damage_Ground_Mul` (3293, `1.0`) per frame (`0x80869938`).
- Wall and ceiling bounce: `DamageFlyReflect` (`0x47` wall, `0x48` ceiling)
  triggers above `Damage_Fly_Reflect_Speed` (param 3284, `1.0`), zeroing
  gravity and reflecting knockback across the touched line's normal times param
  3285 (`0.8`). Only upright walls and level ceilings are followed past contact.

Not predicted: teching, slope or corner bounces, `Down`'s animation motion,
wind, moving platforms, or subsequent hits.

## Frame data

`src/extra/frame_data.cpp`

What this is built on (RSBE01 rev 2, Project+ 3.2.1). READ: read in game code
in a memory dump; MEASURED: verified by `scripts/frame_tests.py` (Mario, Fox,
Bowser and Jigglypuff) comparing predicted actionable frames against the
trackers' live reading; ASSUMED: not yet verified.

- **Threads (READ):** `soAnimCmdModuleImpl` (vtable `0x80AE70D8`) holds at
  `+0x20` a vector of 11 control units (16 bytes each from `+0x1C`):
  interpreter, address-pack array, index, and 16-bit type mask (0 `ActionMain`
  `0x01`, 1 `SubactionMain` `0x02`, 2-3 subaction graphics/sound `0x40`, 4
  `SubactionOther` `0x02`, 5-8 second animation, 9 concurrent loop `0x04`, 10
  hidden action thread `0x10`). `acCmdInterpreter` (`0x80139748`) keeps its
  current command at `+0x0C`, first command at `+0x10`, stack at `+0x20`, and
  clock at `+0x24`.
- **Commands (READ):** 8 bytes (group, type, arg count, option, arg pointer);
  each argument is 8 bytes (kind 0 int, 1 scalar x 60000, 2 pointer, 3 bool, 5
  variable, 6 requirement; value). Group 0 are interpreter commands (`0x80409520`).
- **Timers (READ):** Each thread step adds the step to its clock, then runs
  commands until a timer holds. Type 2 (`Wait until frame`) holds until
  `clock >= arg`; type 1 (`Wait`) adds `arg` to the running threshold.
- **Order in a frame (READ):** Update phase runs status code, then
  `soMotionModuleImpl::updateFrame` (`0x8071EB48`). Fix-position phase
  (`0x80710084`) runs subaction threads (`0x02`, skipped in hitlag), then
  applies queued status changes or runs action threads (`0x1D`, only hidden in
  hitlag), then `soStatusModuleImpl::checkTransition` (`0x8077F0C4`).
  MEASURED: after spot dodges and rolls on all four tested fighters, a jab on
  the predicted frame comes out and one frame earlier does not.
- **Animation rate (READ):** Group 4 type 7 (`Frame Speed Modifier`) sets rate
  (`0x8072464C`) or multiplies rate with two args (P+ hook `0x80724820`).
  `soAnimChr::isEnd` (`0x8072A00C`): non-looping and `frame >= count - 1`.
- **Opening interrupts (READ):** Group `0x64` type 0 (`Allow Interrupt`,
  `0x8084B834`) enables transition term groups for the situation unless blocked
  by LA-Bit 11, 24, 43, or 45; group 2 type `0xA` enables one group.
- **Body collision (READ):** Group 6 type 5 (`soCollisionHitModule::setWhole`:
  0 normal, 1 invincible, 2 intangible, `0x807504C4`). MEASURED on spot dodges
  and rolls across all four tested fighters.
- **Hitboxes (READ/ASSUMED):** Group 6 types `0`/`0x15` offensive collision,
  `0x2B`/`0x2C` thrown, `3`/`4` remove, `0xA`/`0xD` grab box, `0xF` throw.
- **Hitlag (READ):** `soStopModuleImpl::update` (`0x80772480`) decrements hit
  stop before update asks `isStop`; hit stop `n > 0` freezes for `n - 1` more
  frames and runs on the `n`-th. MEASURED on jabs, tilts, and smashes.

Which structures and command IDs matter was learned from ProjectMCodes'
SuperTraining; the interpreter timers were read in the Brawl decompilation and
verified in memory dumps.

### Frame advantage lookahead

On a grounded attacker's hit on shield, the predicted advantage is
`defender_actionable - attacker_actionable`, posted immediately when the hit
lands (`Shield Adv.\n%+d f`), and replaced only if the measured tracker result
differs (`Shield Adv.\nActual: %+d f`). Hits on hurtbox and airborne hits on
shield are logged and left to the live measurement because landing timing and
L-cancelling can change the outcome.

## Lab tools

`src/extra/lab_tools.cpp`

Implements the backend for **Slot Management > Modify Inputs** (`Alter Inputs`
page in the match module): reads and writes individual frames of a recorded
input slot (stick/C-stick `-80..80`, analog triggers `0..150`, and fighter
button bitmask) through the menu scratch buffer (`ui/pages/alter_inputs.h`).

## Character RNG

`src/extra/character_rng.cpp`

Verified by `scripts/rng_tests.py` on RSBE01 rev 2 with Project+ 3.2.1.

### Peach

- **Forward smash:** Project+'s start script cycles three weapons via status
  work flags 104-106 (club -> pan -> racket) rather than using `ft_peach`'s
  random draw. Writing the predecessor flag while Peach is outside her smash
  statuses forces the next forward smash weapon; returning to Default leaves
  the cycle advancing from that weapon.
- **Turnip pull:** Down special (`0x115`) checks parameter 24001 (1 in `N`,
  `N = 80` in `FitPeach.pac`; item table has Mr. Saturn weight 3, Bob-omb and
  Beam Sword weight 0). Setting param 24001 to `-1` forces a turnip, `1`
  forces Mr. Saturn, and Default restores `80` (also restored on module detach).
- **Turnip face:** `ItmCommonParam.pac` (`ItmPeachDaikonParam`) holds weights
  for the 8 faces (`0` Smile 34/58, `1` T Eyes 6/58, `2` Line Eyes 5/58, `3`
  Circle Eyes 3/58, `4` Arch Eyes 3/58, `5` Winky 4/58, `6` Dot Eyes 2/58, `7`
  Stitch Face 1/58). Overwriting the loaded table weights (restored on detach)
  makes the turnip's own start script draw the chosen face, thrown damage, and
  texture pattern frame on frame 1, with a per-frame backstop on held turnips.

### Luigi

`FitLuigi.pac` misfires on side special (`0x118`) when instance int 91 `>= 7`
(and stored-misfire flag 113 is clear); Wait re-rolls int 91 (`0..7`) each
frame while idle. Writing int 91 to `7` (Always) or `6` (Never) after every
game frame controls the launch without disturbing stored misfires.

### Mr. Game & Watch

`ft_gamewatch` reads the displayed Judge number (`0..8` for `1..9`) from
instance int 67 on side special (`0x113`) start, then rolls the next number
avoiding ints 67 and 68. Setting int 67 to the chosen value and int 68 to a
distinct value after each frame forces every Judge; returning to Default leaves
one queued Judge before normal rolls resume.
