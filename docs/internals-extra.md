# Internals of the Extra Module

This reference documents how the `extra` module (`src/extra/`, loaded into MEM2 `AssistFigureResource`, plus `INFO_EXTRA_SRCS` compiled into `extra`) interacts with Brawl and Project+ v3.2.1 (`RSBE01` rev 2). Statements marked "measured" were verified live in Dolphin; "read" means verified in disassembly or memory dumps; "assumed" marks unverified paths.

The `extra` module links directly against `main`'s exported symbols (`build/main/exports.lst`) using far-call trampolines (`scripts/pack-rel.py --far-calls extra`) so MEM2 calls reach MEM1 seamlessly, and registers its callbacks in `main`'s `ExtraHooks` table (`src/main/extra.h`) on attach.

## DI Display (`src/extra/di_display.cpp`)

Verified frame by frame by `scripts/di_tests.py` (Mario vs. Fox on Final Destination and Battlefield):

- **Kinetic energies (`soModuleEnumeration + 0x7C`):** Array at `+0x4C` (12 bytes each: pointer, id). ID `1` (`+0x58`) is gravity (speed `x`/`y` at `+0x8`/`+0xC`, fall accel `+0x10`, max fall speed `+0x14`); ID `4` (`+0x7C`) is knockback (pre-DI launch speed during hitlag).
- **Trajectory DI:** Applied on the first frame after hitlag. Scales stick onto a circle of `1.0025` (`DirectionalInfluence.asm:25-88`) and rotates launch vector by `18 * c^2 * sign(c)` degrees, where `c` is the perpendicular stick component (`-1..1`), preserving speed magnitude.
- **ASDI and SDI:** ASDI shifts on the final hitlag frame by `ASDI_dist * hit_sdi_mul (damageLog + 0x30) * stick` when `|stick| >= threshold` (`0.7`, param `3173` at `0x80B88350`). SDI shifts by `SDI_dist * stick` on hitlag frames crossing into a new 8-way direction (never downward through floor). P+ Code Menu updates SDI (`6`), ASDI (`3`), and knockback decay (`0.051`) every frame at `0x80B88354`, `0x80B88358`, and `0x80B88534` (plus wall-bounce multiplier `3285` at `0x80B88510`); each is range-checked before use.
- **Stage raycasts:** Calls Brawl's `stRayCheck` (`0x809326D8`, `sora_melee + 0x227CC4`) over `g_grCollisionList` (`0x8049E57C`), using coarse 4-frame chords refined frame by frame on contact (0.1 to 0.3 ms total draw time per fighter).
- **Post-contact dashed path:** Models non-teched `Down` ground slides (decelerating by `Ground_Brake` param `3004 * Damage_Ground_Mul` `3293`) and upright-wall / level-ceiling bounces (`DamageFlyReflect` `0x47`/`0x48` above speed `1.0` param `3284`, reflected by `0.8` param `3285`).

## PSA Frame Data and Lookahead (`src/extra/frame_data.cpp`)

Verified against live trackers by `scripts/frame_tests.py` (Mario, Fox, Bowser, Jigglypuff):

- **Threads (read):** `soAnimCmdModuleImpl` (vtable `0x80AE70D8`) holds at `+0x20` a vector of 11 control units (16 bytes each: `acCmdInterpreter*`, address-pack array, id, 16-bit type mask covering `ActionMain`, `SubactionMain`, graphics/sound, `SubactionOther`, second anim, concurrent loop, and hidden action thread). `acCmdInterpreter` (`0x80139748`) holds current command `+0x0C`, first command `+0x10`, command stack `+0x20`, and logical clock `+0x24`.
- **Commands & timers (read):** 8-byte commands (`group`, `type`, `argCount`, `option`, `argPtr`) with 8-byte arguments. Group `0` controls interpreter flow (`0x80409520`): type `1` (`SyncWait`) adds to wait threshold; type `2` (`AsyncWait`) holds until `clock >= arg`.
- **Per-frame order (read & measured):** Update phase runs status code and `soMotionModuleImpl::updateFrame` (`0x8071EB48`). Fix-position phase (`0x80710084`) steps subaction threads (`0x02`, paused in hitlag), applies status changes or action threads (`0x1D`), then calls `soStatusModuleImpl::checkTransition` (`0x8077F0C4`).
- **Animation rate, interrupts, body state, hitboxes, hitlag (read):** Group `4` type `7` sets/multiplies FSM (`0x8072464C`, P+ hook `0x80724820`); `soAnimChr::isEnd` (`0x8072A00C`); Group `0x64` type `0` (`Allow Interrupt`, `0x8084B834`, gated by LA-Bit `11`/`24`/`43`/`45`); Group `2` type `0xA` enables one transition group; Group `6` type `5` sets `soCollisionHitModule::setWhole` (`0x807504C4`); Group `6` types `0`/`0x15`/`0x2B`/`0x2C`/`3`/`4`/`0xA`/`0xD`/`0xF` control hitboxes and grabs; `soStopModuleImpl::update` (`0x80772480`) decrements hitlag before `isStop`.
- **Shield advantage lookahead:** On a grounded hit on shield, predicts `defender_actionable - attacker_actionable` immediately (`Shield Adv. %+d f`) and updates only if the live measured result differs (`Actual: %+d f`).

## PSA AnimCmd 11-Thread & FSM Inspector (`src/extra/psa_inspector*`, `psa_opcodes.*`)

Concept adapted from fudgepop01's SuperTraining / FudgeMenu (with permission) and built cleanly on verified P+ v3.2.1 runtime layouts:

- Reads `soAnimCmdModuleImpl` (`soModuleEnumeration + 0x6C`) -> `m_animCmdThreads` (`+0x20`, vtable `+0x18` `size()`, `+0x5C` `atIndexFast()`), inspecting all 11 threads' `acCmdInterpreter` frame speed (`+0x08`), current/start command pointers (`+0x0C`/`+0x10`), wait stack (`+0x20`, SyncWait/AsyncWait), and logical clock (`+0x24`), alongside `soMotionModuleImpl` (`+0x08`) frame (`+0x40`), primary/secondary rate (`+0x4C`/`+0x7C`), and subaction kind (`+0x58`/`+0x88`).
- Decodes opcodes via compressed lookup tables generated by `scripts/gen_psa_opcodes.py`. Read-only; allocates its working buffer in `Heaps::Network`.

## Unified Frame Meter & Action Log (`src/extra/action_log.*`, `action_log_core.h`)

- Maintains a dual-row (P1 + CPU) chronological ring buffer of `32` cells in 16:9 (`24` in 4:3) paired with live exchange frame advantage (`+` P1 ahead, `-` CPU ahead) and an 8-entry transition log exposed in RAM (`TMAL` debug block v2).
- Each cell stores a 4-bit `Sf6CellKind` (`Startup`, `Active Strike`, `Active Grab`, `Recovery/Lag`, `Stun/Hitlag`, `Advantage Gap`, `GALINT`, `Ledge`, `Ledge Drop`, `Dodge`, `Jump`, `Empty`) plus bit 7 (`kSf6XluBit`) so unactionable intangible frames draw a crisp 2.5 px Ice-Aqua top cap while grounded actionable intangibility fills full Ice-Aqua.
- All cell colors resolve through `ui::stateColor()` in `src/main/ui/state_colors.h`, keeping Frame Meter, Ledgedash HUD, Drill HUD, and Colorblind Mode 100% synchronized.

## Stage Options and Geometry (`src/extra/stage_options*`, `stage_geom*`)

- **Structural stage detection (`stage_options_logic.h`):** Classifies stages from live loaded modules, `Ground` model indices, and bone names rather than hardcoded `srStageKind` tables, so alt-stage reskins work automatically and static alts stay safely untouched. Covers Fountain of Dreams, Dream Land, Smashville, Green Hill Zone, Dead Line, Luigi's Mansion, Delfino's Secret, Yoshi's Story, Yoshi's Island (Brawl), and generic moving-collision stages.
- **Live control hooks:** Toggles P+ `m_isHazardOff` (`gmGlobalModeMelee + 0x29` bit `0x20`, restored on match exit/detach if overridden), pauses platform animations via `Ground + 0x6D` bit `0x10` (`m_isPauseAnim`), clears `grCollisionJoint + 0x54` bit `0x80` on instant frame teleports to prevent phantom carry velocity, and drives exact P+ v3.2.1 `STDT` / `CHR0` frames (for example FoD spout frames `0`/`39`/`43`/`57`/`64`/`75`).

## Character RNG, Fighter Mechanics, and Olimar Lab Kit

- **Character RNG (`src/extra/character_rng.cpp`):**
  - **Peach:** Forces Forward Smash predecessor status flags `104`..`106`, overrides down-special param `24001` (`-1` turnip, `1` Mr. Saturn, `80` default restored on detach), and overrides `ItmPeachDaikonParam` face weights (restored on detach) with a held-turnip backstop.
  - **Luigi:** Writes LA-Int `91` (`7` Always, `6` Never) after each step without disturbing stored misfire flag `113`.
  - **Mr. Game & Watch:** Sets LA-Int `67` (current Judge `0..8`) and a distinct LA-Int `68` after each step.
- **Fighter Mechanics (`src/extra/fighter_mechanics*`):** Manages live charge, resource, float, and Waddle Toss overrides across 15 fighters directly (plus delegates Olimar to `olimar_lab.*`, 16 total in `extra`) using verified P+ v3.2.1 work variables. Ice Climbers **Nana Throw** runs inside the `main` module (`src/main/cpu/driver.cpp` calling `applyNanaThrowOverride` in `src/main/cpu/nana_throw_core.h`).
- **Olimar Lab Kit (`src/extra/olimar_lab.*`, `olimar_pikmin_logic.h`):** Controls Pikmin pull-pool bitmask (R/Y/B/W/P), global and per-color maturity (Leaf/Bud/Flower), Whistle Lead sorting, 4-slot starting lineup, and D-Pad instant pluck / 16-preset lineup bindings.

## CPU, Combo, Scenario, and Swap Services

- **PPlusAI Recovery (`src/extra/pplus_recovery*`):** 44-fighter offstage recovery engine adapted from fudgepop01's PPlusAI with stage geometry awareness (`stage_geom.*`) and four mixup profiles (`Mixed`, `Low`, `High`, `Direct`).
- **Combo Trainer (`src/extra/combo_trainer*`):** Manages automatic post-hitstun resets, noclip warp (`Correct_None` restored next frame), random/grounded stage positions, fixed/random damage, recovery trainer loops, post-hitstun escape options, and 7 toggleable floating overhead meters (independent of Combo Trainer auto-reset).
- **Adaptive CPU & Multi-Slot Reversals (`src/extra/adaptive_cpu*`, `reversal_slots*`):** Tracks recent player habits (`Track` / `Adaptive`) and weights Slot 1 vs. Slot 2 reversals across four triggers (`On Hit`, `On Shield`, `On Getup`, `On Ledge`).
- **Scenario Presets (`src/extra/scenario_presets*`):** Applies curated training setups while snapshotting prior user settings so SD saves project back to the pre-preset view (`extraScenarioSavedView`).
- **Mid-Session Fighter Swap (`src/extra/fighter_swap*`):** Swaps fighters and costumes inside Training with cleanup barriers and safe post-swap savestates.
- **Lab Tools (`src/extra/lab_tools.cpp`):** Implements **Slot Management > Alter Inputs** frame editing (`-80..80` sticks, `0..150` triggers, button mask) through `ui::menuScratch()`.

## Visualizers, UI Services, Credits, and Skill Drills

- **Visualizers:** `hitbox_heatmap*` (world-space AABB and attack persistence bands), `hitbox_colors*` (`Property` / `Priority` coloring), `grab_visualizer*` (3D grab, search/tether, and item-pickup collision overlays), `input_viewer*` (live controller overlay), `timing_meter*` (overhead timing bar), and `tech_sparkle*` (native `ef_common` wavedash/shorthop sparkle cues).
- **Native UI Assets, Descriptions, and Summaries:** `ui_assets*` renders native Brawl/P+ button emblems and stock icons; `menu_descriptions.cpp` (generated by `scripts/gen_menu_descriptions.py`) stores compressed option help text; `menu_summaries.cpp` computes live right-aligned submenu status pills (`<= 15` chars); `src/main/ui/info_display.cpp` and `src/main/game/status_names.cpp` (generated by `scripts/gen_status_names.py`, Canonical Huffman + LZSS decompressed into `Heaps::Network`) compile into `extra` via `INFO_EXTRA_SRCS`.
- **Shared Credits Modal (`src/common/credits_modal.h`, `src/extra/credits_modal.*`):** Zero-heap, hardware-scissored 60fps scrollable credits modal shared between `extra` (Lab Menu `Credits` utility via `ExtraHooks::creditsHook`) and `menu` (Boot Menu `Z: Credits`).
- **Skill Drill and Challenge Engines:** `drill_core_engines.*`, `drill_hover_ps_engines.*`, `drill_pplus_tech_engines.*`, `drill_practice_driver.*`, `drill_hud.*`, `pplus_tech_tracker.*`, `combo_break_rush.*`, and `challenge_backends.*` (the last gated by `STAGE4C=1`).
