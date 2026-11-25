# Files on the SD card

The plugin keeps its files in one folder next to the build's own files:
`<MOD_PATCH_DIR>tm/`, which on Project+ is `/Project+/tm/`. The folder is
created on the first save. Nothing else on the card is written.

All numbers are big-endian, as the console stores them. The game pads every
write to a multiple of 32 bytes; readers use the size in the header.

## Common header (32 bytes)

| Offset | Size | Field |
|--------|------|-------|
| 0x00 | 4 | magic: `TMSF` (settings), `TMBF` (bests) or `TMRF` (recording) |
| 0x04 | 2 | format version of that kind of file (1) |
| 0x06 | 2 | header size (32) |
| 0x08 | 4 | size of the file before the padding |
| 0x0C | 4 | checksum: FNV-1a (32-bit) of bytes 0x10 to size |
| 0x10 | 4 | sequence: settings and bests, +1 per save; recordings, 0 |
| 0x14 | 12 | zero |

A file whose magic, sizes or checksum are wrong is *damaged*. A file with a
higher format version is from a *newer* version and is never read; one with
a lower version is *older*.

## Settings: `settings-a.bin`, `settings-b.bin`

The game's file library has no rename, so a save cannot go to a temporary
name first. Instead the settings are written to the two files in turn: each
save goes to the file that does not hold the newest settings, with the
sequence one higher. A write cut short damages only that file; at boot the
newest valid file is read.

After the header:

| Offset | Size | Field |
|--------|------|-------|
| 0x20 | 2 | area count (16) |
| 0x22 | 2 | settings layout of the writer (`main::kSettingsVersion`, 1) |
| 0x24 | 4 | zero |
| 0x28 | 8 each | area table: id (1), generation (1), used bytes (2), offset in the file (4) |
| ... | | the areas' bytes, each padded to 4 |

An area is one feature group of the lab's settings, stored as it is in
memory without its reserved bytes:

1. General (13 bytes: percents/locks, frame advance, Ledgedash HUD, camera,
   game speed, move staling, DI Display, Player Infinite Shields)
2. Controls (10 bytes: advance button/delay, menu button, D-pad bindings,
   frame rewind button, P+ Code Menu shortcut, Menu Style)
3. Display Options (12 bytes: debug overlay, HUD, hitboxes, fighter models,
   stage collision, stage model, effects, Hitbox Heatmap options, Grab /
   Tether Visualizer)
4. HMN Info Display (10 bytes)
5. CPU Info Display (10 bytes)
6. OSD Menu (22 bytes)
7. CPU Options (47 bytes: CPU percent/lock, CPU behavior/DI/SDI/ASDI/Dual-Stick
   SDI, counters, shields, tech/get-up, recovery, ledge option, clatter escape,
   adaptive mode)
8. Recording (28 bytes: random percents, slot modes/weights, playback flags,
   Start On Input)
9. HMN Color Overlays (18 bytes)
10. CPU Color Overlays (18 bytes)
11. Tech Invisibility (2 bytes)
12. CPU Advanced Counter tables (71 bytes)
13. Character RNG (4 bytes, generation 1: Peach Turnip, Peach Forward Smash,
    GnW Hammer, Luigi Misfire)
14. Stage Options (4 bytes: hazards, platform mode, Fountain of Dreams left
    and right heights)
15. Combo Trainer (11 bytes: enable, noclip, fix/randomize position,
    ground-only random, randomize/fixed damage, recovery trainer, timer
    adjustment, enabled floating meters bitmask)
16. Fighter Mechanics (23 bytes: per-fighter charge, resource, float, and Nana
    Throw overrides)

**Forward/backward compatibility.** A setting is only ever added at the end of
its area, so the first bytes of an area keep their meaning. Reading copies the
bytes both the file and this version know; settings the file lacks keep their
defaults; an area missing from the file, or of another generation (a setting
changed meaning; the generation goes up), keeps its defaults; an area this
version does not know is skipped. Every value is then clamped to its row's
range, and any retired choice value falls back to its row's default.

**Kept for the session only.** These are stored as 0 and never read back,
so every boot starts with their defaults: Frame Advance, Player Percent and
its lock, Game Speed, CPU Percent and its lock, CPU Control, Combo Trainer
Enable (`comboTrainer.enabled`), Debug Overlay, Match HUD, Fighter Models,
Stage Model and Effects, and every Recording row except Random Percent
(recordings do not outlive a match; Export keeps them). Temporary Scenario
Preset overrides are also projected back to the pre-preset snapshot before
saving so activating a preset never overwrites your saved settings.

**When.** Read once per session, when the first Training match starts.
Written when the lab menu closes, only if a stored setting changed. A damaged
file is not overwritten until a setting changes; while a newer-format file is
the newest on the card, no settings file is written for the session (changes
stay in memory, so the newer file is kept untouched).

## Bests: `bests-a.bin`, `bests-b.bin`

Persistent per-slot best scores, written in turn to two files as settings are.

After the header:

| Offset | Size | Field |
|--------|------|-------|
| 0x20 | 2 | slot count (32) |
| 0x22 | 2 | rules revision (1) |
| 0x24 | 2 each | best score per slot (slot 0 unused); 0 is none |

Read once per session on Training's character select, and written back only
when a best changes. On a locked card bests stay in memory for the session.
Reset Settings does not touch them.

## Recordings: `YYMMDD-HHMMSS.tmr`

Named after the console's date and time when exported (`-2`, `-3`, ... if
the name is taken). Written once, under a new name, so a cut-short write
leaves a damaged file and nothing else.

After the header, 64 bytes the browser lists:

| Offset | Size | Field |
|--------|------|-------|
| 0x20 | 32 | name (the file name without `.tmr`) |
| 0x40 | 4 | HMN: character kind, costume file, costume, character select icon |
| 0x44 | 4 | CPU: the same |
| 0x48 | 2 | stage kind |
| 0x4A | 2 | Project+'s alternate-stage button word at the start |
| 0x4C | 2 | length: the longest slot, in frames |
| 0x4E | 2 | slots with inputs, a bit each: HMN, CPU |
| 0x50 | 7 | year (2), month, day, hour, minute, second |
| 0x57 | 1 | 1 if the next 4 bytes were found |
| 0x58 | 4 | the character select's +5 and +6 bytes of HMN and CPU (the costume as the select recorded it) |
| 0x5C | 1 | stage hazards off (P+ `m_isHazardOff`: 0 hazards on, 1 hazards off; 0 in older files) |
| 0x5D | 3 | zero |

Then: area count (2), slot count (2), offset of the placements (4); the
Recording, CPU Options and CPU tables areas (table and bytes, as in the
settings file); two placements of 32 bytes (HMN, CPU: x, y, z, facing, damage,
shield health as floats, shield present (1), 7 zero); and each slot: side (1),
index (1), first and end frame (2 each), used entries of the four stick tables
(2 each), used (L, R) pairs (2), frames stored approximately (4), then the
tables' float words, the pairs, and 8 bytes per frame (buttons (2), four table
indices, the pair's index, the press state).

**Import** starts a Training match with the file's characters, costumes and
stage, puts the slots and those areas back (the CPU's damage and lock stay as
they are), places both fighters at the stored positions with the stored
facing, damage and shield, and saves the positions there as Save Positions
does, so playback is exact from that point in the new match. Not reproduced:
the original match's random state, stale-move queues and everything else the
savestate held (animation frames, the camera, items, projectiles, charges);
fighters stored in the air are put back in the air and fall from there, as the
lite start state does. A file is refused as damaged - leaving existing slots
and settings untouched - if it is shorter than the headers and placements,
fails its checksum or area bounds, or holds out-of-range slot headers, palette
indices, palette floats (outside `-2..2` or non-finite), or placement floats
(positions outside `-10000..10000`, facing outside `-1..1`, damage outside
`0..999`, or, when shield present is nonzero, shield outside `-200..200`, or
non-finite).
