# Internals

This reference documents how the `main` module (`src/main/`) interacts with Brawl and Project+ where the source code alone does not explain it: fixed addresses, struct offsets, engine rules, and provenance. Source comments cross-reference sections here (for example, `docs/internals.md, "Technique timings"`).

Unless noted otherwise, all addresses and measurements are for NTSC-U Brawl (`RSBE01` rev 2) running Project+ v3.2.1. "Read in code" means read from Brawl or Project+ disassembly/source; "header" means declared in `lib/BrawlHeaders`; "observed" and "measured" mean confirmed live in Dolphin (with setup noted when recorded).

## Fighters and Frames

### Fighter Values

`src/main/game/fighters.cpp` locates active fighters and reads/writes their core match values:

- **Entries:** `ftManager::getEntryCount`, `getEntryIdFromIndex`, `getOwner`, `getFighterOperationType` (header, `sora_melee` symbols). Each `ftEntry` (`ftEntryManager::getEntity`) holds `m_activeInstanceIndex` (`+0xA`), `m_instances[]` `{ftKind, Fighter*}` (`+0x30`), and `m_playerNo` (`+0x58`).
- **Human vs. CPU:** `getFighterOperationType` returns `0` for a human and non-zero for a CPU (matches `gmGlobalModeMelee::m_playersInitData[player].m_state` at `+0x98 + 0x5C * player + 1`, where `0` is human and `1` is CPU).
- **Modules:** `Fighter -> StageObject::m_moduleAccesser (+0x60) -> m_enumerationStart (+0xD8) -> soModuleEnumeration`.
- **Position:** `soPostureModule +0xC` (`x`, `y`, `z` floats); previous frame position at `+0x18`.
- **Status kind:** `soStatusModule +0x34` (for example `0x00` Wait, `0x1B` Guard, `0x44` DamageAir, `0x45` DamageFly, `0x16` Landing).
- **Work variables:** `soWorkManageModule +0x20` points to instance ("LA") work and `+0x24` points to status ("RA") work. Each `soGeneralWorkSimple` stores its int array pointer at `+0xC` and count at `+0x10`.
- **Hitstun remaining:** LA-Int `0x38` (held during hitlag, then decrements by 1 per game frame to 0).
- **Shield stun remaining:** RA-Int `5` while status is `GuardDamage` (`0x1D`).
- **Damage:** `ftOwner::getDamage` / `setDamage` (vtable `+0x10`, matching Training's CPU Damage call at `0x80962594`; equals float at `ownerData + 0x24`).

### Fighter Body Values

`src/main/game/body.cpp` performs read-only per-frame inspection for technique timings, color overlays, and tech invisibility:

- **Position and velocity:** `soPostureModule +0xC` and `+0x18`.
- **Stick and buttons:** `soControllerModule::getStickX`, `getStickY`, `getSubStickY`, `getButton`, `getTrigger`, plus LA-Float `35` for P+ C-stick Y.
- **Intangibility and invincibility:** `soCollisionHitModuleImpl` holds `m_collisionHitGroupArray` at `+0x58`, with the body `soCollisionHitGroup` at `+0xC` (validated by checking its `m_posX` at `+0x8` matches the fighter's `x` within `0.5`). `m_whole` (`+0x1C`) and `m_global` (`+0x20`) hold `HitStatus` values: `0` Normal, `1` Invincible, `2` Intangible (XLU), `3` None (hurtbox group disabled). `hitGroupIntangible()` mirrors `soCollisionHitGroup::getTotalStatus` (`0x8074D2F4`, `max(0, m_whole, m_global)`) and marks `intangible = 1` only for `1` or `2` (never state `3` `None`). Remaining frames live at `m_xluFrameGlobal` (`+0x24`) and invincibility frames at `+0x28`.
- **Status work flags ("RA" bits):** `soWorkManageModule +0x24`, flag words at `+0x1C` (count at `+0x20`). In word 0: bit 6 is short-hop, bit 30 is aerial landing lag enabled (clear means autocancel), bit 2 is P+'s fastfall flag.
- **L-cancel:** LA-Int `90` (set to 7 on aerial shield press, 8 in hitlag; decremented per frame; halves landing lag if `> 0` on landing).
- **Grab breakout timer:** `soControllerModule::getClatterTime(0)` (counts down 1 per frame and 8 per mashed input).
- **Powershield:** LA-Bit `96` (instance flag word 3, bit 0; set by P+'s `PowerShield.asm` on a powershield).
- **Project+ extra fighter data ("Additional Info System"):** Pointer stored at LA-Basic `180` (`ints + 0x2D0`), past the nominal int count, matching P+'s hooks at `0x808152E4` and `0x8073A2A4`.

### Fighter Trackers

`src/main/game/tracker.cpp` records every simulated game frame per fighter via Brawl's native observer lists:

- **Observers:** Registers `soStatusEventObserver` and `soAnimCmdEventObserver` instances using `soStatusEventObserver::addObserver` (`sora_melee .text+0x133C8`) and `soAnimCmdEventObserver::addObserver` (`+0x677C`), and unregisters them with `eraseObserver` whenever the match pauses, ends, takes a savestate, drops a CPU count, or detaches.
- **Ordered intra-frame events:** Records multiple status transitions within a single frame (`Tracked::events`) because frame-perfect P+ inputs pass through several statuses in one step (for example `JumpSquat -> Jump -> EscapeAir -> LandingFallSpecial`).
- **Frame values:** Status, hitstun, shield stun, hitlag (`soStopModuleImpl::m_hitStopRealFrame`), airborne (`Situation_Air`), interrupt transition groups `1..0x12`, AnimCmd interrupt/cancel commands (group 2 types `10`/`11` and group `0x64` type `0`, adapted from ProjectPunch's `anim_cmd_watcher.cpp`), and attack connection (`soCollisionAttackModuleImpl::m_isInflict`).
- **"Actionable during a frame":** Adapts ProjectPunch's `occupied*` tracking: entering a free status at any point in the frame, having an action interrupt group open, or having the cancel window open outside hitlag/hitstun/shield stun marks the frame actionable.

### Frame Hand-Over

Each simulated step is handed to trackers and listeners exactly once at whichever fires first: `trackerHandOver` (render callback at `0x8001792C`, so HUD updates appear on the same picture) or `trackerStep` (the next game step's pad hook at `0x8002A7A8`, handling multi-step bursts or error-manager frames that skip render).

In `gfApplication`'s main loop (`0x800171B4`..`0x80017508`):

1. `gfPadSystem::updateSystem` (`0x800171CC`): system pad hook (savestates, Lab Menu).
2. Zero, one, or more game steps (`fn_80017618` from `0x80017358`), each hitting `updateGame` (`0x8002A7A8`) and `ipSwitch::update` (`0x8004AA70`) before running scene tasks.
3. Render (`0x80017468`, hitting render hook `0x8001792C`).
4. Frame counter increment (`0x80017504`).

`trackerSuspend` detaches all observers before a savestate copy so observer pointers are never baked into RAM snapshots; `trackerResume` re-attaches them (and drops history after a load). `trackerFrame` stays consistent across render and pad callbacks so CPU timing comparisons remain exact.

### Technique Timings

`src/main/game/techniques.cpp` derives read-only technique callouts from tracker event sequences (frame 1 = earliest possible frame under Project+'s rules):

- **Wavedash:** `JumpSquat -> Jump -> EscapeAir -> LandingFallSpecial` (`0x19`). Reports airborne frame, angle below horizontal (`16.2` to `22.0` degrees green, up to `30.0` yellow; reflects P+'s shallowest stick `(96, -28)` = `16.26` deg), hop type, and jump-squat hold length (adapted from ProjectPunch's `wavedash_info.cpp`).
- **L-Cancel:** Checks P+'s LA-Int 90 counter (`> 0` on `LandingAttackAir`), reporting `landing - press + 1` out of P+'s 7-frame window (`N/7`).
- **Act OoS / Act OoLag / Act OoHitstun / Act OoAirborne:** Counts frames from the earliest actionable frame out of shield stun (through `Guard_Off`), landing/throw/getup/tech lag (shown up to `kActOoLagShown = 15`), hitstun, or airborne jump/fall/platform drop/ledge drop.
- **Jump Cancel:** Grab, up smash, or special directly out of `JumpSquat` (earliest on squat frame 2 = frame 1).
- **Fastfall:** From first airborne frame with `vy <= 0` to P+'s fastfall flag (RA-Bit 2).
- **Combo Counter & SDI:** Hits landed without an actionable frame in between, plus distinct stick directions and actual position shifts during hitlag (excluding final-frame ASDI).
- **Dashback:** P+ `TurnDash` or `SlowTurn.asm` 2-frame turn-into-dash success rate per fighter and match.
- **Powershield:** Shield hit on `GuardOn` frame 1-2 with LA-Bit 96 set (fresh press); reports early frames or held-press misses.
- **Tech Lockout:** Reports remaining frames of the 40-frame lockout (`kTechLockout`, 41f apart techs) when a missed tech had a refused guard press within the 20-frame window.
- **Boost Grab / Item Throw:** Dash grab (`Catch_Dash`) or up smash out of dash attack (frames 2-3), or `Item_Throw` out of roll (glide toss) or airdodge.
- **Grab Breakout (Mash Out):** Live `getClatterTime(0)` countdown and mash rate per grabbed frame.
- **Ledgedash:** `CliffWait -> airborne -> grounded actionable`; reports remaining GALINT (or negative vulnerable frames down to `-24`), adapted from ProjectPunch's `ledge_tech.cpp`.

### Frame Advantage

`src/main/game/advantage.cpp` adapts ProjectPunch's on-hit and on-shield frame advantage calculation (`defender_actionable - attacker_actionable`, within `+-49f`, waiting past the configurable tolerance so multi-hit pressure continues the exchange). Key improvements over ProjectPunch:

- Attributes hits via tracker slot (`m_isInflict`) rather than player index, supporting Ice Climbers partners and multi-player matches.
- Recognizes attackers already actionable on the hit frame (projectiles).
- Re-arms only on a new connecting hit (not a whiff) and expires stalled exchanges after `kGiveUp` frames.

### Color Overlays and Hiding

`src/main/game/overlays.cpp` drives Brawl's color-blend and visibility modules:

- **Color tinting:** Calls `soColorBlendModule::setSubColor(color, enabled)` (`m_subColor` `+0x14A`, `m_useSubColor` `+0x14F`, verified against `isEnableSubColor()` before first use). Tints follow the end-of-frame status except `Actionable`, which follows the tracker's intra-frame actionable flag.
- **Hiding:** Uses `soVisibilityModule::setWhole(0)` (`+0xE`, which also suppresses `soShadowModuleImpl`'s shadow projection bit `+0x44` bit `0x02`) plus `Fighter::setCursor(false)` / `setNameCursor(false)` (`Fighter+0x138` bits `0x04`/`0x02`). Because `Fighter::processFixCamera` (`0x8083A8B0`) re-enables overhead tags every frame, an inline hook at `0x8083ABDC` (`sora_melee .text+0x1301C8`) calls `game::overlaysFixCamera` to keep cursors hidden for hidden fighters. Pre-hide state is saved and restored cleanly on unhide, pause, transform, savestate, or detach.
- **Tech Invisibility:** Ground techs (`0xBE`..`0xC0`) hide on `stateFrame > dist + delay && stateFrame < 21` using P+'s per-character first distinguishable frame (`kDistinguishable` in `tech_frames.cpp`), while Tech Sound fires at `stateFrame == dist`. Get-ups and slip get-ups (`0xAF`..`0xB2`, `0xB8`..`0xBB`, `0xE6`..`0xE9`) hide on `5 + delay < subactionFrame < 20` (adapted from ProjectPunch's `invis_techs.cpp`).

## The CPU

### Source Organization (`src/main/cpu/`)

`src/main/cpu/options.cpp` holds the CPU state machine (`Brain`) in a single translation unit so CodeWarrior's `-ipa file` optimization stays effective with zero virtual dispatch overhead. To keep maintenance clean, its implementation is split into guarded internal includes:

- `brain.h`: `Brain` layout plus pure state-machine helpers (`shieldHitRefreshed`, `canSpoofWallTech`, `nextTrackerStep`, `rebaseFrame`, `rebaseCueFrames`, `resetActionState`) compiled directly into host unit tests via `TM_CPU_BRAIN_PURE_ONLY`.
- `options_di_sdi.hpp`: TDI, ASDI, SDI, Dual-Stick SDI, and custom stick logic.
- `options_tech.hpp`: Landing prediction, tech/get-up/wall-tech timing, and banana slip techs.
- `options_counters.hpp`: Ground/air/shield counters, SHFFL/FFL timing, Advanced Counter tables, Custom Counter Pool, and Multi-Slot Reversals.

### Live CPU Reads and Engine Rules

`src/main/cpu/view.cpp` reads live fighter state during input evaluation:

- **Facing:** `soPostureModule +0x40` (`+1` right, `-1` left).
- **Shield health:** LA-Float `3` (`Float_Guard_Shield`; P+ full shield is `60`).
- **Knockback velocity:** Kinetic module damage energy (`+0x14 -> +0x4C`, speed at `+0x8`/`+0xC`), matching P+'s `CustomAI.asm` and `Ledge.asm`.
- **Landing interrupts:** `Landing` (`0x16`) opens transition groups once motion frame passes customize parameter `3074` (`Landing_Frame`); `LandingLight` (`0x17`) uses parameter `3073` (verified at `0x808746B8`).
- **Analog trigger variables:** P+'s `ButtonPresses.asm` writes live L/R triggers into LA-Float `36`/`37` (previous frame in `40`/`41`) and trigger state into LA-Int `77`. `hideAnalogTrigger` clears suppressed triggers and recomputes LA-Int `77` so Frame Advance bindings never leak light shield.
- **Smash DI suppression:** To stop pure TDI from triggering unwanted SDI on the final hitlag frame, `holdStickStill` copies the stick into `soControllerImpl`'s previous stick (`+0x10`/`+0x14`) and sets X/Y flick counters (`+122`/`+124`) to at least `kHeldFlick` (6), safely past Brawl's 4-frame flick window (param `23041`).
- **Fastfall conditions:** General term `10006` requires `stickY <= -0.665`, downward crossing past `-0.35` within 4 frames, `vy < 0`, and hitstun over (never runs in `DamageFall`).
- **ASDI vs. TDI frames:** P+ reads ASDI on the last hitlag frame and TDI on the first frame after hitlag.
- **CPU tech timing:** 20-frame tech window, 40-frame lockout (`kTechGap = 41`, params `23052`/`23053`), collision raycast from raised tumble ECB bottom with knockback decay (`3294`) and fall-speed cap, neutral stick held until `kTechRollLead` (1 frame before landing) to avoid premature tumble wiggle (`3299` = `0.75`) or drift, then roll magnitude between `0.2` (`3198`) and `0.75`. Grounded banana trips (`Slip` `0x89`, `Slip_Down` `0x8A`) unlock tech once `IC_Basic[0] >= 20.0`.

### Training Pause Damage Guard and Match Exit

- **Training pause CPU damage (`src/main/game/training_damage.cpp`):** `stOperatorInfoTraining::processBegin` (`0x809620CC`) rewrites every CPU's damage when Training's pause menu closes (`0x80962594` `setDamage`, `0x809625BC` `setStartDamage`, `0x809625EC` HUD setter `0x800E14A4`). While the player has not edited Training's own CPU Damage value (`IfMinigameTraining +0x28`), `P+TMM` temporarily NOPs those three calls so lab-set or accumulated CPU damage survives opening Training's pause menu.
- **Return to CSS (`src/main/game/match.cpp`):** Ends the match through Training's native Finish path; all cleanup (flushing pending SD saves, freeing savestate/recording memory, removing tracker observers) runs cleanly in `detach`.

## Display Options, Match Timer, and 2D GX Rendering

### Display Options (`src/main/display/display.cpp`)

- **Scene layers:** `gfSceneRoot::setLayerDispStatus` (`0x8000D234`, bit `0x80` at `+0x399 + 0x18 * layer`): layer `8` holds the HUD and Training pause menu, layer `0` holds the stage/background, layers `4` and `5` hold most effects. `P+TMM` only restores layers it hid itself (preserving P+ Code Menu's own toggles) and re-syncs its hidden-layer flags after savestate loads.
- **Move Staling off:** Clears each owner's `ftLogPatternModule` (`ownerData + 0xCC8`) via `clearPattern` (`0x808E01E4`) before every game step.
- **Game Speed:** Overrides `gfApplication`'s frame rate (`u16` at `+0xF8`; `60` = `1x`, `50` = `5/6x`, `40` = `2/3x`, `30` = `1/2x`, `15` = `1/4x`) and latches system-pad "pressed" bits across skipped-step passes so inputs are never dropped in slow motion.
- **Camera Mode:** `CameraController::getInstance` (`0x8009BE74`, kind at `+0x40`): Zoom calls `IfMinigameTraining::setCameraType(..., 2)` (`0x8010535C`), Fixed enforces `CameraController::changeInput(0xC)` (`0x8009C69C`), Normal leaves camera control to Training.

### Brawl's Match Timer (`src/main/ui/match_hud.cpp`)

Drives Brawl's top-center `IfCenter` timer (`IfMngr + 0x44`, model `+0x154`) by setting flags `0x80 | 0x40`, calling setup (`0x800DDB54`, zero-alloc), and updating frames via `0x800DDCF8` (small digits display frames `00`..`59`). Individual player damage boxes (`IfMngr + 0x4C` -> `IfPlayer` parts `+0x28` -> `ScnObj + 0xCC` bit `0x10`) can be hidden selectively without hiding the timer.

### Hitbox and Collision Drawing (`src/main/display/debug_draw.cpp`)

Calls Project+ v3.2.1's Gecko C2 `renderDebug` routines directly from the render hook so overlays remain visible during Frame Advance and independent of Code Menu rewrites:

- Hurtboxes: `soCollisionHitModuleImpl::renderDebug` (`0x80750F58`, module `+0x20`, slot `+0xD8`).
- Hitboxes: `soCollisionAttackModuleImpl::renderDebug` (`0x8074BDB0`, module `+0x1C`, slot `+0x120`, invoked with `r31` set to the fighter's module accesser). When **Hitbox Colors** is set to `Property` or `Priority`, `main::extraDrawColoredHitboxes()` renders colored hitboxes first and falls back to P+'s default draw if unavailable.
- Shields, reflectors, absorbers: modules `+0x24`, `+0x28`, `+0x2C`, slot `+0x88` (recolored by P+ hook `0x807513F4`).
- ECB: ground module (`+0x10`, vtable at `+0x8`), slot `+0x21C` (P+ patches `0x80137850`/`0x80137860`).
- Stage collision: walks `g_grCollisionList` (`0x8049E57C`) calling P+'s helper `0x80541F90` for entries with flag `0x80`.

Every entry point is validated against expected instruction words before use. Both `boxesAvailable()` and `collisionAvailable()` also gate on `*(volatile const u8*)0x805483FF == 1` (`DebugFileLoader` completion flag confirming `/menu3/data.debug` is loaded at `0x80548400`), preventing uninitialized draws during early load frames.

### 2D GX Primitives, Hardware Scissoring, and Semantic Colors

- **GX state hygiene (`src/main/gfx/draw.cpp`):** `gfx::begin()` activates camera 4 (`gfDrawSetupCoord2D`) and tracks shape vs. text GX state to avoid redundant state resets. `gfx::end()` unconditionally restores `GXSetNumTevStages(1)`, `GXSetLineWidth(6, 0)`, and `GXSetCullMode(GX_CULL_BACK)` before restoring camera 0 (`setGX()`), ensuring 2D overlays never leak GX state into Brawl's 3D pipeline.
- **Hardware GX scissoring (`pushScissor` / `popScissor`):** Queries `GXGetViewportv` and `GXGetScissor`, maps 640x480 logical coordinates into live EFB pixels, intersects with the active scissor rect, and calls `GXSetScissor`. Used by the Lab Menu's sliding Highlight Lens and clipped list scrolling, and by the shared Credits modal.
- **Two-layer semantic palette (`src/main/ui/state_colors.h`):** Separates Layer 1 timing/grading colors (`Good`, `Okay`, `Bad`, `Neutral`, `Muted` for OSDs, drills, and frame advantage) from Layer 2 action-state valence colors (`Actionable`, `GALINT/XLU`, `Startup`, `Active Strike`, `Active Grab`, `Flat Matte Dead Ash-Slate Lag`, `Hot Amber-Orange Stun`, `Ledge`, `Drop`, `Dodge`). Both layers route through `Display > Theme > Colorblind Mode` (`Off`, `Deuteran`, `Protan`, `Tritan`) with zero extra draw calls.
- **Compact FIFO OSDs (`src/main/ui/osd.cpp`):** Renders per-player append-only FIFO stacks with compact translucent washes, a 3px bottom lifetime countdown bar over a darkened track, 4-way outlined text, 7-frame `easeOutCubic` entry/glide motion, and 4-frame fade-out. On console, the message ring array is heap-allocated in `Heaps::Network` to conserve `OverlayMenu` `.bss`.

## Savestates and Recordings

### Savestate Preconditions, Feedback, and Initial State

Before saving or loading at the top of the main loop, `P+TMM` verifies: a running unpaused Training match; exactly two fighter entries (`No. of CPUs == 1`); empty `gfFileIOManager` queues; every fighter on instance 0 and not in a Final Smash; no Lab Menu, pause camera catch-up, or P+ debug pause in progress; and (for loads) a valid slot from the same match whose heap layout, archive signature, and sampled payload checksum still match.

On the first running frame of a Training match, `P+TMM` silently captures an initial state so D-Pad Left always has a valid reset point before the first manual save. Saving plays `kSoundConfirm` with a brief flash (skipped in Frame Advance); loading plays `kSoundBack`; refusals play `kSoundError` and post a red OSD for structural failures.

### Savestate Region Set and Real-Wii Safety (`src/main/savestate/snapshot.cpp`)

The snapshot lives in `Fighter4Resource` and `Fighter3Resource` (empty in 1v1 matches) and copies:

- **Gameplay heaps:** pool header, every allocated block, and free-block headers.
- **Active fighter instance heaps:** entire range (including free space) for byte-exact post-load determinism.
- **Resource heaps:** non-archive blocks only (loaded `"ARC\0"` files are checksummed as an archive signature instead of copied).
- **REL `.data`/`.bss`:** every loaded REL module except `P+TMM`'s own modules (`extra`'s pool is excluded via `setOwnExclusionAt(1, ...)`).
- **Fixed DOL and Project+ ranges:** RNG state, task IDs, slow manager, camera, collision heads, effect system, and P+ scratch areas.

Explicitly excluded from fixed ranges (never copied): GX FIFO tail (`0x80611F40`, `0x20`), thread stack bottom (`0x805B7660`, `0x20`), `nw4r::ef` draw strategies (`0x804A3E90`, `0x578`), framebuffer bookkeeping (`0x805B5034`, `0x20`), Project+ SD stream / `FPC_PATH` / RSBE soundbank state (`0x805A7450`, `0x900`), and Project+'s SDHC/SDXC/SDUC extension flag (`0x805A9350`, `0x4`). Sound, pads, threads, OS, file IO, GPU FIFO, framebuffers, `P+TMM` modules, and the slot buffers themselves are never copied.

To keep audio DMA and hardware interrupts safe on real Wii hardware, copies run inside `OSDisableScheduler` (`0x801E0760`) / `OSEnableScheduler` (`0x801E079C`) with `GXDrawDone` + cache invalidation, using 16 KiB Broadway L1D-friendly tiles (`kRestoreTileBytes`) and yielding interrupts every 64 KiB (`kIrqYieldBytes`) so `MSR[EE] == 0` spans stay well below the AX audio DMA period.

### Step Budget and Rewind Burst

`gfApplication::mainLoop` (`0x80016EE0` / `fn_80016F8C`) steps the game by subtracting `frameRate (+0xF8) * pendingFrames` from the step budget (`s32` at `+0xFC`) and adding `60` per executed step. Recording rewind sets the budget to `1 - 60 * (k - 1)` to execute `k` steps silently in one render pass, clearing `gfPadSystem + 0x30` (`consumeFrameCounter` `0x800293DC`) so Brawl does not catch up with 3-step bursts afterward.

### Lite Position Restore

Restoring saved positions without a full savestate (`src/main/record/start_state.cpp`) cancels active hitstop, zeroes hitstun, warps via `Fighter::warp`, calls `attachGround(0)` (preventing pass-through on platforms) and `setStGroundForce(0)` (preventing landing lag) when grounded, then calls `changeStatusForce(...)` to enter the resolved status (`Wait` or `Fall`), restores air-jump count, and clears knockback speed alongside `clearSpeedAll`.

## The Lab Menu and Info Display

### Info Display Data Sources (`src/main/ui/info_display.cpp`)

`info_display.cpp` and `status_names.cpp` (274 common status names stored as Canonical Huffman + LZSS and decompressed once into `Heaps::Network`) are compiled into the `extra` module via `INFO_EXTRA_SRCS` to maximize `main` OverlayMenu headroom. Non-tracker Info Display rows read directly from game memory:

- **Self / KB Velocity:** `soKineticModule::getSumSpeed` with masks `1` (motion/gravity/drift/stop) and `2` (damage knockback).
- **System Stick / C-Stick / Trigger:** Game pads (`gfPadSystem + 0x444`) using P+'s stick scale (`cap 100 * 0.010325`) and analog C-stick scale (`* 0.011775`).
- **Engine Stick / Trigger:** `soControllerImpl` live main stick and trigger (`+0x38`); engine C-stick (`+0x20`/`+0x24`) is always zeroed by Brawl (`0x80764ED0`) and marked `NA`.
- **Ledgegrab Timer / Jumps / Walljumps:** LA-Basic `2` (param `23050`), LA-Basic `1` vs. param `23003`, LA-Basic `3` vs. param `3224`.
- **Can Walljump / Wall Cling:** IC-Basic `23189` + LA-Basic `4` (`20f` cooldown IC-Basic `23069`); IC-Basic `23190` + LA-Basic `12`/`11` (Knuckles `0x2D` reports `Climb`).
- **Intangibility / Shield Health / Grip Strength:** Body hit group `+0x24`/`+0x28`, LA-Float `3`, `getClatterTime(0)`.
- **ECB Lock / ECB Bottom / Line Info / Blastzones:** P+ extra info `+0x10`/`+0x18`, `getDownPos(0) - pos`, ground collision status `+0xD4`, and `CameraController +0x158..+0x164`.

### Adding a Lab Menu Row or Page

Menu limits (`src/main/ui/menu.h`, `src/main/storage/format.h`): `ui::kMaxPages = 48`, `ui::kMaxDepth = 6`, `ui::kScratchBytes = 112`, `storage::kMaxAreas = 31`. Each page lives in `src/main/ui/pages/` and is registered in `PageId` order inside `registry.cpp`.

To add a stored option:

1. Allocate its field from the front of the matching feature area's `reserved` array in `src/main/settings.h` (never reorder existing fields).
2. Add the row to the page's table (`def` supplies its default; conditional rows pass an `enabled` predicate via `TM_ROW`; `kFlagHideWhenDisabled` compacts inactive rows unless `Show Inactive Fighters` is enabled).
3. Add its description to `scripts/gen_menu_descriptions.py` (keep inline `Option::desc` NULL in `main`) and re-run any affected `scripts/gen_*.py` generators.
4. Run `make check-host` to validate storage bounds, layout, and Brawl font text-fit rules.
