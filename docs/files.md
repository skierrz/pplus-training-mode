# Files on the SD Card

Project+ Training Mode Mod (`P+TMM`) stores all of its persistent files in one folder next to the build's own files: `<MOD_PATCH_DIR>tm/`, which on Project+ is `/Project+/tm/`. The folder is created on the first save. Nothing else on the card is written.

All multi-byte numbers are big-endian, matching the Wii's native layout. Brawl's file library pads every write to a multiple of 32 bytes; readers use the exact byte length recorded in the header.

## Common Header (32 Bytes)

| Offset | Size | Field |
|--------|------|-------|
| `0x00` | 4 | Magic: `TMSF` (settings), `TMBF` (bests), or `TMRF` (recording) |
| `0x04` | 2 | Format version for that file kind (`1`) |
| `0x06` | 2 | Header size (`32`) |
| `0x08` | 4 | Logical file size before 32-byte write padding |
| `0x0C` | 4 | Checksum: 32-bit FNV-1a over bytes `0x10` through `size - 1` |
| `0x10` | 4 | Save sequence: `+1` per save for settings and bests (`0xFFFFFFFF` wraps to `1`); `0` for recordings |
| `0x14` | 12 | Zero |

A file whose magic, size bounds, or checksum fail validation is treated as *damaged*. A file with a higher format version is from a *newer* version and is never read; one with a lower format version is *older*.

## Settings: `settings-a.bin`, `settings-b.bin`

Brawl's file library has no atomic rename, so a save cannot be written to a temporary file and swapped in. Instead, `P+TMM` alternates writes between `settings-a.bin` and `settings-b.bin`, writing each save to whichever slot does not hold the newest valid file with a sequence number one higher. A power loss or interrupted write damages only the slot being written; on the next boot, the newest valid file is loaded.

After the 32-byte common header:

| Offset | Size | Field |
|--------|------|-------|
| `0x20` | 2 | Area count (`17` for full saves; `1` to `4` for Boot Menu Quick Setup partial saves) |
| `0x22` | 2 | Settings layout version of the writer (`main::kSettingsVersion`, currently `2`) |
| `0x24` | 4 | Zero |
| `0x28` | 8 each | Area table (up to `storage::kMaxAreas = 31` entries): `id` (1), `generation` (1), `used` bytes (2), file `offset` (4) |
| `...` | variable | Raw bytes of each stored area, each padded to a 4-byte boundary |

The maximum settings file buffer size is `storage::kSettingsFileCap = 768` bytes (`areas.cpp` verifies at compile time that all 17 areas with their reserved tails fit within 768 bytes).

### Stored Feature Areas (Layout v2)

Each area represents one feature struct inside `main::Settings`, written without its trailing `reserved` bytes (`areas.cpp`):

1. **General** (15 bytes, gen 1): Player percent/lock, Frame Advance, Ledgedash HUD options, Camera Mode, Game Speed, Move Staling, DI Display, Player Infinite Shields, Offline Versus opt-in, and Frame Meter (Action Log) target.
2. **Controls** (10 bytes, gen 1): Frame Advance button and hold delay, Lab Menu Button, D-Pad Left/Right/Up/Down bindings, Frame Decrement button, P+ Code Menu shortcut toggle, and Menu Style (`Category Rail` / `Top Tabs`).
3. **Display** (16 bytes, gen 1): Debug Overlay, Match HUD, Hitboxes, Fighter Models, Stage Collision, Stage Model, Effects, Hitbox Heatmap options, Grab / Tether Visualizer, Live Input Viewer, Hitbox Colors, DI Vector preset, and PSA Debugger target.
4. **HMN Info Display** (10 bytes, gen 1): Preset, text size, and 8 row selections.
5. **CPU Info Display** (10 bytes, gen 1): Preset, text size, and 8 row selections.
6. **OSD Menu** (31 bytes, gen 1): Position, Shield/Hit Frame Advantage toggles and tolerance, 17 OSD toggles, Max Messages, OSD background color/opacity, Lab Menu accent, Info Display background color/opacity, Overhead Timing Bar mode, OSD Preset, Tech Sparkle mode, and Colorblind Mode.
7. **CPU Options** (64 bytes, gen 1): CPU percent/lock (3 bytes) and `cpu::CpuOptions` (61 bytes: control, behavior, TDI, SDI/ASDI/Dual-Stick SDI, ground/air/shield counters and delay, Auto L-Cancel, SHFFL Timing, Multi-Slot Reversals, Shield Timing, infinite shields/health/angle, intangible, tech/get-up modes and weights, grab escape/release, controlled-by port, tech sound/trap/lockout/miss-wait, active Advanced Counter editor fields, offstage recovery, on-ledge option, clatter escape, adaptive mode, and P+ Tech Tracker OSD).
8. **Recording** (28 bytes, gen 1): HMN/CPU Random Percent, HMN/CPU modes and slots, Mirrored playback, CPU Counter trigger, Loop, Auto-Restore, Start Paused, Takeover, per-slot random weights, Slot Management fields, and Start On Input.
9. **HMN Color Overlays** (18 bytes, gen 1): Preset and 17 state-group color choices.
10. **CPU Color Overlays** (18 bytes, gen 1): Preset and 17 state-group color choices.
11. **Tech Invisibility** (3 bytes, gen 1): Mode, delay, and pre-tech aerial drift.
12. **CPU Tables** (98 bytes, gen 1): `ready` flag, 10 `AdvancedHit` pages (70 bytes), `poolReady`, three 64-bit Custom Counter Pool bitmasks (Ground, Air, Shield), and custom TDI stick X/Y.
13. **Character RNG** (4 bytes, gen 1): Peach Turnip, Peach Forward Smash, Mr. Game & Watch Judge, and Luigi Misfire.
14. **Stage Options** (4 bytes, gen 1): Stage Hazards, Moving Platforms mode, and Fountain of Dreams Left/Right heights.
15. **Combo Trainer** (16 bytes, gen 2): Enable, No-Clip, Fix Position, Randomize Position, Ground Only, Randomize Damage, Recovery Trainer, Reset Timer Adjustment, Fixed Damage, 7-bit floating meter mask, Fixed Damage Mode, Reset P1, Reset Delay, Post-Hitstun Escape, and Recovery Mixup.
16. **Fighter Mechanics** (32 bytes, gen 1): Per-fighter charge, resource, float, and Waddle Toss overrides, Ice Climbers Nana Throw, and bit-packed Olimar Lab Kit settings (legacy single-color/maturity, Pull Pool, Whistle Lead, per-color maturity, D-Pad Mode, 4-slot starting lineup, and 4 D-Pad preset IDs).
17. **OSD Presets** (12 bytes, gen 1): Three 32-bit saved OSD toggle masks (`Custom 1`, `Custom 2`, `Custom 3`).

### Compatibility and Upgrades

- **Additive field rule:** New settings are always appended to the end of their area (taken from its `reserved` tail) so existing bytes keep their meaning. Reading copies `min(file.used, current.used)` bytes; newly added trailing fields keep their defaults; areas missing from the file or carrying a different `generation` reset to defaults; unknown areas are skipped. Every loaded value is then clamped to its row's valid range, sanitized, and checked against retired choice labels.
- **Upgrading from v1 (`layout < 2`):** When a valid layout v1 save is detected on the first Training match of a session, `P+TMM` holds the file intact and opens an interactive **Update Saved Settings** modal offering three choices: **Reset All to New Defaults (Recommended)**, **Reset Updated Options Only**, or **Keep Existing Settings**. If the match ends before a choice is confirmed, the prompt appears again on the next match.
- **Silent v2 upgrades:** Layout v2 files from earlier `pre4` builds upgrade silently without a warning banner and queue a full v2 save automatically. This covers 16-area v2 files (before Area 17 `OsdPresets`), Boot Menu Quick Setup partial saves (1 to 4 areas: `General`, `Display`, `Osd`, and optionally `Cpu`), and Area 6 (`Osd`) growing from 30 to 31 bytes (`colorblindMode`).
- **Newer-version protection:** If the newest file on the card has `format > 1` or `layout > 2`, it is marked newer and ignored for the session, and `P+TMM` refuses to write either settings slot so the newer save stays untouched on SD. Even if a future version writes a file larger than `kSettingsFileCap` (768 bytes), the reader verifies the actual on-card file size covers the header's `size` before treating it as newer rather than damaged.

### Session-Only Fields

The following fields are always zeroed before writing and preserved untouched across loads so every fresh boot starts in a predictable state:

- `General`: Player Percent, Lock Player Percent, Frame Advance, Game Speed
- `Display`: Debug Overlay, Match HUD, Fighter Models, Stage Model, Effects
- `CPU`: CPU Percent, Lock CPU Percent, CPU Control
- `Combo Trainer`: Enable (`comboTrainer.enabled`)
- `Recording`: every field from `hmnMode` onward (only HMN and CPU `Random Percent` persist; recordings themselves do not outlive a match unless exported)

Temporary overrides applied by an active **Scenario Preset** are also projected back to the pre-preset snapshot before saving, so activating a preset never overwrites your saved lab configuration.

### When Settings Are Read and Written

Settings are read once per session when the first Training match starts, into the stub's persistent session block. Writes run asynchronously when the Lab Menu closes, and only if the checksum of the saved view changed (or a reset/upgrade forced a write). On match exit, `settingsDetach()` drains any in-flight write and flushes one queued follow-up save (`s_again`) before unloading, so closing the menu and immediately leaving the match does not drop changes. A damaged file is not overwritten until a setting changes.

## Bests: `bests-a.bin`, `bests-b.bin`

Persistent per-drill best scores use the same alternating two-file scheme as settings (`src/menu/bests_file.cpp`).

After the 32-byte common header (`TMBF`, format `1`):

| Offset | Size | Field |
|--------|------|-------|
| `0x20` | 2 | Slot count (`32`, `drills::kBestSlots`; buffer cap `320` bytes allows up to 128 slots) |
| `0x22` | 2 | Scoring rules revision (`1`) |
| `0x24` | 2 each | Best score per slot (slot 0 unused; `0` means no score yet) |

Bests are read once per session on Training's character select and written back only when a best improves. Newer-format best files are preserved untouched (including files larger than the 320-byte buffer cap when the on-card size covers the header's `size`). On a locked SD card, bests remain in memory for the session. **Reset Settings** does not clear bests.

## Recordings: `YYMMDD-HHMMSS.tmr`

Exported recordings are named from the console's calendar clock (`-2`, `-3`, and so on if the name already exists). Each export is written once under a new name, so an interrupted write leaves only that damaged file.

After the 32-byte common header (`TMRF`, format `1`), a 64-byte `RecordingInfo` block holds the metadata listed by the Recordings browser:

| Offset | Size | Field |
|--------|------|-------|
| `0x20` | 32 | Name (filename without `.tmr`) |
| `0x40` | 4 | HMN: character kind, costume file, costume, character select icon |
| `0x44` | 4 | CPU: character kind, costume file, costume, character select icon |
| `0x48` | 2 | Stage kind |
| `0x4A` | 2 | Project+ matched alternate-stage button word at match start |
| `0x4C` | 2 | Length of the longest non-empty slot, in game frames |
| `0x4E` | 2 | Non-empty slot bitmasks: HMN (1 byte), CPU (1 byte) |
| `0x50` | 7 | Export timestamp: year (2), month, day, hour, minute, second |
| `0x57` | 1 | `1` if character-select costume bytes below were captured |
| `0x58` | 4 | Character-select `+5` and `+6` costume bytes for HMN and CPU |
| `0x5C` | 1 | Stage hazards flag (Project+ `m_isHazardOff`: `0` hazards on, `1` hazards off; `0` in older files) |
| `0x5D` | 3 | Zero |

Following `RecordingInfo`:

1. `RecordingHead` (8 bytes): `areaCount` (2), `slotCount` (2), and byte offset of the placements block (4).
2. Three stored settings areas (`Recording`, `CPU Options`, and `CPU Tables`) using the same area table and payload layout as `settings-*.bin`.
3. Two 32-byte `Placement` records (HMN, then CPU): `x`, `y`, `z`, facing (`lr`), `damage`, and `shield` as big-endian floats, followed by `haveShield` (1 byte) and 7 zero bytes.
4. One block per exported slot: a 20-byte `SlotHead` (`side`, `index`, `start`, `end`, four stick float-table counts, `(L, R)` pair count, and approximate-frame count), the float tables and `(L, R)` pairs, and 8 bytes per recorded frame (button mask, four table indices, `(L, R)` pair index, and press state).

### Import Behavior and Validation

Importing a `.tmr` file starts a Training match with the recorded fighters, costumes, stage, alternate stage, and hazard flag, restores the recorded slots and settings areas (leaving CPU percent and lock alone), warps both fighters to their saved placements with their saved facing, damage, and shield, and establishes those positions as the recording start state.

Because `.tmr` files store placements and inputs rather than a full RAM savestate, imports do not restore the original match's RNG state, stale-move queues, animation frames, camera, items, projectiles, or fighter charges; fighters exported mid-air start in the air and fall from that position.

A `.tmr` file is rejected as damaged (leaving existing slots and settings untouched) if it is shorter than the headers and placements, fails its checksum or area bounds, or contains out-of-range slot headers, table indices, float palette values (non-finite or outside `-2..2`), or placement values (non-finite, position outside `-10000..10000`, facing outside `-1..1`, damage outside `0..999`, or shield outside `-200..200` when `haveShield` is non-zero).
