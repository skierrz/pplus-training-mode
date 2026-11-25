# Internals

This is a reference for how the main module (`src/main`) works where the
code alone doesn't say it: the game's addresses and offsets it relies on,
the rules it follows, and where each of those facts comes from. It's
written for someone who already knows Brawl's code and modding. The code
points here with a short comment, for example
`docs/internals.md, "Technique timings"`.

Unless a section says otherwise, everything here is for RSBE01 rev 2 with
Project+ 3.2.1. "Read in code" means read in the game's code or in P+'s
source. "Header" means a declaration in `lib/BrawlHeaders`. "Observed" and
"measured" mean seen in the running game, with the characters and stage
named where they were recorded and marked "setup not recorded" where they
weren't. "Unchecked" and "not checked" mark what hasn't been confirmed.

## Fighters and frames

### Fighter values

`src/main/game/fighters.cpp` finds the match's fighters and reads the few
values the lab reads and changes on them. Everything here is for RSBE01
rev 2 with Project+ 3.2.1. "Header" means a `lib/BrawlHeaders` declaration;
"observed" means read in Training matches (Mario vs Fox on Final
Destination; `scripts/lab_tests.py` repeats them).

- **Entries:** `ftManager::getEntryCount` / `getEntryIdFromIndex` /
  `getOwner` / `getFighterOperationType` (header, sora_melee symbols). An
  entry's `ftEntry` (`ftEntryManager::getEntity`) holds
  `m_activeInstanceIndex` (`+0xA`), `m_instances[]` {ftKind, Fighter*}
  (`+0x30`) and `m_playerNo` (`+0x58`): header layout, observed at those
  offsets (in Training, entry 0 was the CPU, player 1; entry 1 Mario,
  player 0).
- **Human or CPU:** `getFighterOperationType` is 0 for the human and
  non-zero for the CPU (observed; the test checks it against the match
  setup's per-player human/CPU byte).
- **Controller port of a human:** its player number (observed for port 1 in
  Training; other ports not verified).
- **Modules:** Fighter -> `StageObject::m_moduleAccesser` (`+0x60`) ->
  `m_enumerationStart` (`+0xD8`) -> `soModuleEnumeration` (header;
  observed).
- **Position:** `soPostureModule` `+0xC` (x, y, z floats), previous position
  `+0x18`. Observed: Mario spawns at x -60, the CPU at +60; x grows while
  running right; both are equal while standing.
- **Status kind:** `soStatusModule` `+0x34`. Observed: 0 Wait, `0x1B` Guard
  while shielding, `0x44` when hit by a forward smash, `0x16` landing after
  it.
- **Work variables:** `soWorkManageModule` `+0x20` -> the instance ("LA")
  work, `+0x24` -> the status ("RA") work. Each `soGeneralWorkSimple` holds
  its int array at `+0xC` and its length at `+0x10` (observed).
- **Hitstun left:** LA int `0x38`. Observed: 25 when hit by Mario's forward
  smash at 0%, held through hitlag, then down by one per game frame to 0.
- **Shield stun left:** RA int 5 while the status is GuardDamage (`0x1D`)
  (observed when shielding a jab; the test checks it).
- **Damage:** `ftOwner::getDamage` / `setDamage` (header virtuals).
  Training's own CPU Damage option calls vtable slot `+0x10` (`setDamage`)
  with (percent, false) at `0x80962594` (read from memory), which matches
  the header's order; `getDamage` equals the float at `ownerData+0x24`
  (observed).
- **Match setup:** `gmGlobalModeMelee::m_playersInitData[player].m_state`
  (header; `+0x98`, `0x5C` each, state at `+1`): 0 human, 1 CPU. The menu
  module writes 0 for the human; `scripts/nav.py` counts 1 as a CPU.
  Lowering Training's No. of CPUs from 3 to 1 changed the dropped players'
  state on the step its pause menu closed, and their entries went on the
  next step (observed, frame by frame).

### Fighter body values

`src/main/game/body.cpp` reads the values of one fighter that the trackers
don't keep, for the technique displays, the colour overlays and the tech
invisibility. It only reads. Everything here is for RSBE01 rev 2 with
Project+ 3.2.1. "Header" means a `lib/BrawlHeaders` declaration;
"observed" means read in Training (Mario and Fox on Final Destination);
"checked" means `scripts/display_tests.py` repeats it on every run.

- **Position:** `soPostureModule` `+0xC`, previous `+0x18` (see "Fighter
  values").
- **Stick and buttons:** `soControllerModule::getStickX` / `getStickY` /
  `getButton` (header virtuals). P+'s own L-cancel code calls the same
  module at the vtable offsets the header gives `getButton` and
  `getTrigger`.
- **Intangibility:** `soCollisionHitModuleImpl`'s hurtbox group array
  (header `m_collisionHitGroupArray`, observed at `+0x58`) holds the body's
  `soCollisionHitGroup` at `+0xC` (observed: its `m_posX` follows the
  fighter's x, which is checked before it's trusted). `m_global` (`+0x20`)
  is 2 while ledge or respawn intangibility runs, and `m_xluFrameGlobal`
  (`+0x24`) holds its frames left: 31 on the first CliffWait frame, then
  one less per frame (observed; checked). `m_whole` (`+0x1C`) is 2 during
  CliffCatch. `+0x28` holds the invincibility frames left
  (`getInvincibleFrameGlobal` reads it; the info display's test compares
  it).
- **Status work flags ("RA" bits):** `soWorkManageModule` `+0x24` -> the
  status work, its flag words at `+0x1C` (count at `+0x20`, 2 observed).
  In word 0, bit 6 is set at the end of a jump squat whose jump button was
  let go (short hop; ProjectPunch's `common.h` names it); bit 30 is on
  while an aerial's landing lag is on (ProjectPunch's "enable landing lag";
  clear means autocancel); bit 2 turns on on the frame a fastfall starts
  (observed; P+'s `Physics.asm` calls RA-Bit 2 "the fastfall flag";
  checked).
- **L-cancel:** LA int 90 (instance work `+0x20`, ints at `+0xC`). P+'s
  `L-Cancel.asm` sets it to 7 when shield is pressed in an aerial (8 in
  hitlag), takes one off per frame, and on landing halves the landing lag if
  it's above 0. Observed: 4 on landing after a press 3 frames before, and
  landing lag 7 frames instead of 15 for Mario's neutral air (checked).
- **Grab timer:** `soControllerModule::getClatterTime(0)` (header virtual).
  Observed on the grabbed fighter: the float at `+0xC` of what the module's
  `+0x10` points to, 81.07 on the first grabbed frame (Fox at 0%), one less
  per frame and 8 less per mashed input; the grab breaks when it runs out.
  `scripts/osd_tests.py` reads it there and compares.
- **Powershield:** LA bit 96 (instance work flag word 3, bit 0). P+'s
  `PowerShield.asm` clears it when a shield starts and sets it when the
  powershield sound plays. Observed set on a hit in the first two frames of
  a freshly pressed shield, and clear on later hits (checked).
- **P+'s extra fighter data** ("Additional Info System",
  `Project+/Memory.asm`): its pointer sits at LA-Basic[180] (ints +
  `0x2D0`), past the int count the work reports. P+'s hooks read it there
  without a count check (`0x808152E4`, `0x8073A2A4`; read in code).
  Observed: the count is below 181 in Training.
- **Subaction and its frame:** `motion.cpp`.

### Fighter trackers

`src/main/game/tracker.cpp` keeps a per-fighter record of every game frame,
fed by observers it adds to each fighter's event units. Everything here is
for RSBE01 rev 2 with Project+ 3.2.1. "Header" means a `lib/BrawlHeaders`
declaration (the decompilation's for this disc); "observed" means checked
in Training matches with the emulator tests, by `scripts/display_tests.py`
on every run unless noted.

**Observers.** A fighter's `soEventManageModule` holds one event unit per
kind of event. The Fighter itself observes its status and animation-command
units through its `soStatusEventObserver` and (via StageObject)
`soAnimCmdEventObserver` bases, whose `m_unitID` name the units (header).
Ours copy those unit ids and register with the game's own
`soStatusEventObserver::addObserver` / `soAnimCmdEventObserver::addObserver`
(sora_melee `.text+0x133C8` / `+0x677C`, decompilation symbols), the
functions the game's own observers use. They store the manage id and the
send id (the observer's index in the unit's list). Removal is the header's
`soEventObserver::removeObserver`:
`soEventSystem::getManager(manage id)->eraseObserver(unit, send id)`, which
is what `~soEventObserver` does.

The game never removes our observers, and they live in this module's
memory, so they're on only while the match runs unpaused and isn't ending.
Training's own ways to end or rebuild a match go through its pause menu.
The lab's Exit turns them off before the game's end of the match runs
(`lab.cpp` `exitMatch`); the game removes the fighters a few steps before
the scene exits. Detach takes off whatever is left. The exception is a
lower No. of CPUs: the extra fighters are removed on the frame after the
pause closes, by when the observers were back on. The match setup has
dropped their players on the frame before (observed), so their observers
are taken off then and not put back (`fighters.cpp` `playerInMatch`;
`make test-stress` lowers it every match).

Status changes are recorded in order, with values read at that moment
(`StatusEvent`; fixed capacity per frame, the excess counted and logged),
because several can happen within one game frame. Checked every match
(`scripts/display_tests.py`, `make test-stress`): every registration is
matched by a removal, and each re-registration gets the list index the first
one had (a list index still held would push it to another one). The unit
sizes logged on registration are 32 and 40, the sizes of the
decompilation's `soEventUnitWithWorkArea<..., 32/40>`.

**Frame values**, read at the end of each simulated game step:

- Status kind, hitstun, shield stun: see "Fighter values".
- Hitlag left: `soStopModuleImpl::getHitStopRealFrame` (header field
  `m_hitStopRealFrame`). Observed: set on the hit, down by one per frame
  to 0.
- Airborne: `soSituationModule::getKind() == Situation_Air` (header enum;
  observed 0 standing, 2 in the air).
- Interrupt groups: `soStatusModule::isEnableTransitionTermGroup(group)` for
  groups 1-0x12 (header). Observed: all ground groups on in Wait, none
  during hitstun, shield stun or a move's startup.
- Enable/Disable Interrupt Group commands (animation command group 2, type
  10/11, argument 0 = group) and Enable Cancel (group `0x64`, type 0), from
  the animation-command observer, read as ProjectPunch reads them
  (`anim_cmd_watcher.cpp`, MIT; see `THIRD_PARTY_NOTICES.md`).
- Attack connected: `soCollisionAttackModuleImpl::m_isInflict` (header).

"Actionable during a frame" adapts ProjectPunch's "occupied this frame"
tracking (`playerdata.cpp` `setAction`, its `occupied*` flags). A status
change into a free or chosen status during the frame counts even if the
fighter left it again within the same frame. So does an action interrupt
group, or the cancel window being open at the end of the frame, outside
hitlag, hitstun and shield stun.

### Frame hand-over

Each simulated step's frame is handed to the trackers and then to the frame
listener exactly once. That happens at whichever comes first of
`trackerHandOver` (the render callback, after the game drew that frame) and
`trackerStep` (the next step's pad hook, for a frame no picture followed).
`trackerStepRuns` says which steps count.

The render callback comes first because the stub calls it after the game
drew the frame and before the copy to the framebuffer
(`stub/render_hook.cpp`), and the lab's HUD is drawn in it.
So what a frame decides shows on that frame's picture.

gfApplication's main loop (`0x800171B4`-`0x80017508` on RSBE01 rev 2 with
Project+ 3.2.1, read in code) runs, in order:

1. `gfPadSystem::updateSystem` (`0x800171CC`): the system pad hook, where
   our savestates and the lab's menu run.
2. The pass's game steps (`fn_80017618`, called from `0x80017358`), as many
   times as the pass's time budget gives: none, one, or several. Each step
   reaches `updateGame` (the game pad hook) in ipSwitch before
   `gfScene`/`gfTaskScheduler::process` run the step's tasks.
3. The render (`0x80017468`, `fn_8001789C`, whose `0x8001792C` is the render
   hook).
4. The frame counter (`0x80017504`).

So no task runs between the render callback and the next step's pad hook.
What the calls after the render (`fn_80076D00`, `fn_80023C70`,
`fn_80036C5C`) do is unchecked. A pass with no step draws the same frame
again; a pass with several (the rewind's burst in `record.cpp`) draws once.
A pass whose flag byte (gfApplication `+0xED`, bit `0x10`) sends it down the
other path at `0x80017410` (gfErrorManager's render) never reaches the
render hook. That's why `trackerStep` has its fallback.

`trackerStep` also puts the observers on or off. They're on only while the
match is live (running, not paused, not ending), so that the game's ways of
removing fighters find them off: Finish and P+'s reset go through
Training's pause menu, and the lab passes the end its Exit starts as not
live. A lower No. of CPUs removes fighters a frame after the pause closes;
the setup drops those players first, and a fighter whose player it no
longer lists isn't observed (`playerInMatch`). Whether a step is simulated
is the lab's verdict: a stop set after it in the same pad hook (the
recording's) isn't seen.

**Savestates.** `trackerSuspend` takes every observer off, because the
game's lists must not hold ours when memory is copied, and `trackerResume`
puts them back; with `forget`, the fighters' history is dropped first, as
after a load. After a load whose step before was simulated, the loaded
state is handed over as a frame once more (by the next pad hook, or by the
render callback if one comes first), whether or not the frame before the
load was handed over by its render callback. Either way, the listener's
first frame after a load is the loaded state. When the frame before was
handed over in its render callback, that load adds one hand-over (that frame
and the loaded state), so `trackerFrame` counts one more than the steps from
then on. When it wasn't, the loaded state takes that frame's place and the
count is the steps'.

**`trackerFrame`** counts the frames finished since the match module was
attached; during the frame listener, it's the number of the frame being
handed over. A frame handed over in a render callback is counted at the
next step's pad hook (or the next hand-over, whichever comes first). So the
number reads the same at every point of the main loop, whichever callback
handed the frame over: in that render callback after the hand-over and in
the next pass's system pads, it's the frame on screen's; from the pad hook
on, the next one's. Code there that compares it with a frame number taken
in the listener (for example the CPU's tech timing) relies on
that. It doesn't say whether the frame on screen was handed over:
`trackerFramePending` and `trackerFrameOwed` do.

### Technique timings

`src/main/game/techniques.cpp` works out the technique timings the lab
shows (wavedash frame, L-cancel timing, Act OoS and so on) from the
trackers' frames and hands them to a sink. It reads only; no game state
changes. Frames are tracker frames, and statuses are
`Fighter::Status::Kind` values (`status_ids.h`). A timing is 1 when the
action came on the first frame it could, following Project+'s rules.

"Observed" below means seen in emulator runs on Project+ 3.2.1 with
scripted inputs. `make test-osds` (`scripts/osd_tests.py`: Mario against a
Fox CPU on Final Destination, both read from memory frame by frame)
repeats the frame-1 wavedash's statuses, the L-cancel press on the landing
frame, the dashback outcomes, the powershield frames, the boost grab on the
dash attack's 2nd, 3rd and 4th frames and the tech lockout's length (found
anew on every run). Other characters and stages aren't covered.

Sequences of statuses are followed through each frame's status changes in
the order the game made them (`Tracked::events`), not through the status a
frame ended in. That matters because at frame-perfect timings P+ passes
through several statuses within one frame. Observed: an airdodge on the
first airborne frame goes JumpSquat -> Jump -> EscapeAir ->
LandingFallSpecial in that one frame; on the second airborne frame of a
short hop it goes EscapeAir -> LandingFallSpecial. Values that belong to a
frame (stick, counters, position) are read at its end.

- **Wavedash:** JumpSquat -> Jump -> EscapeAir -> LandingFallSpecial (P+'s
  `AirDodge.asm` lands an airdodge in `0x19`; observed). The frame is the
  airdodge frame counted from the first airborne frame; the angle is the
  stick on that frame as P+'s airdodge reads it, in degrees below horizontal,
  floored to a tenth. Its line is green from 16.2 to 22.0 and yellow up to
  30.0, else white (the bottom 16.2 reflects P+'s shallowest airdodge stick
  (96,-28), 16.26 degrees, after P+'s stick cap at 100 units, its scale of
  0.010325 and its 0.28 dead zone; (96,-27) flies level, observed on Fox).
  "Hop" is the short-hop flag as the Jump began; jump frames are the frames
  jump was held in the jump squat. Adapted from ProjectPunch's
  `wavedash_info.cpp` (MIT; see `THIRD_PARTY_NOTICES.md`).
- **L-cancel:** P+'s rule (`L-Cancel.asm`). It's a success if P+'s counter
  is above 0 on the landing into LandingAttackAir. The press is the frame
  the counter jumped up (to 7, or 8 in hitlag). The frame is
  `landing - press + 1`, so a press on the landing frame itself counts and
  is 1: the aerial's per-frame code runs before the landing (observed:
  counter 7 and halved lag). P+'s window is frames 1-7 ("N/7").
- **Act OoS:** from the frame shield stun ended (GuardDamage left) to the
  first action taken out of the shield, counting on through Guard_Off: the
  drop itself is not the action, and a drop that just ends in Wait shows
  nothing.
- **Act OoLag:** After an aerial's landing, an autocancelled aerial (Landing
  straight out of AttackAir), a wavedash's landing, a throw, a getup or a
  tech, frame 1 is the first frame the fighter could act: inside the landing
  when the game opens its interrupts early (Landing does, at a per-fighter
  frame), else in Wait. The count runs through Wait, walking, turning and
  crouching to the first action and is shown up to frame 15 (`kActOoLagShown`),
  green on 1. A shield, a plain landing, an action out of the Wait an
  autocancel's Landing ends in, or out of a Wait reached by walking or
  crouching shows nothing.
- **Act OoHitstun:** from the first actionable frame after hitstun to the
  first action (none if the fighter techs or grabs a ledge first).
- **Jump cancel:** a grab (added by P+'s `Misc.asm`), up smash or special
  straight out of the jump squat. The frame is the number of frames in the
  jump squat before it; the earliest, on the squat's second frame, is 1.
- **Fastfall:** from the first airborne frame the fighter didn't move up
  (P+ takes a fastfall from that frame on; observed: Fox fastfalls on a
  frame with no vertical movement, Mario on his first frame down) to the
  frame P+'s fastfall flag came on, 1 if they're the same.
- **Combo:** hits taken without being actionable in between (every change
  into a damage status is one, `FighterFrame::hits`), and the damage they
  did.
- **SDI:** during the defender's hitlag, stick moves into a new direction
  (inputs) and frames the fighter was moved (shifts). The last hitlag frame
  (automatic SDI) is left out.
- **Act OoAirborne:** an aerial or special straight out of Jump (from the
  jump squat), a double jump, a fall or a platform drop (Pass), and a double
  jump straight out of Jump. Frame 1 is the frame the fighter entered a jump
  from its squat or a fall, but the frame after a double jump, a drop or a
  ledge drop, since those are that frame's action. A fall counts only for 10
  frames and only if it didn't follow Jump, an aerial, a special, an airdodge
  or a hit, and a jump counts until 5 frames past its peak.
- **Dashback:** P+ turns a back input from standing into TurnDash and then
  Dash when the stick is past the dash sensitivity on the first frame
  (observed). A slower input gives a slow Turn, which P+'s `SlowTurn.asm`
  still turns into a dash if the stick gets there on the turn's second
  frame (observed: Turn, Turn, Dash); after that the turn plays out and the
  fighter walks. Success is TurnDash or a Turn into a dash; a failure is
  still being in the Turn after its second frame with the stick past the
  dash sensitivity. The rate is successes / tries per fighter and match.
- **Powershield:** a shield hit (GuardOn -> GuardDamage) on shield frame n
  (1 = the frame GuardOn began). P+'s `PowerShield.asm` allows it only for
  a fresh press in the first two frames ("no powershield buffering");
  success is P+'s own powershield bit (see "Fighter body values"). A miss
  is "early" by n - 2 frames, or on frames 1-2 without the bit (the shield
  came from a press held before it could start).
- **Tech lockout:** a shield press less than `kTechLockout` frames after the
  press before it doesn't tech (observed with scripted presses: 40 frames
  apart refused, 41 teched; a refused press restarts the count). It's
  reported when the fighter misses a tech (Down) and its last press, inside
  the 20-frame tech window, was refused: the lockout frames that were left
  at that press.
- **Item throws:** an item throw (Item_Throw) straight out of a roll (glide
  toss) or an airdodge. The frame is the roll's or airdodge's frame (1 = its
  first).
- **Boost grab:** a dash grab (Catch_Dash) or an up smash straight out of a
  dash attack. The frame is the dash attack's frame (observed: a grab comes
  out on its 2nd and 3rd frames, not later).
- **Grab breakout:** while grabbed (Capture_Pulled to Capture_Damage), the
  grab timer (see "Fighter body values") every frame and the mash rate:
  timer used up per grabbed frame, the frame's own countdown included.
- **Ledgedash:** CliffWait, then airborne, then actionable on the ground.
  GALINT is the body's intangibility frames left at that frame; once they
  ran out, minus the vulnerable frames counting that frame, so the first
  vulnerable frame shows -1. The OSD hides GALINT past 24 vulnerable frames.
  The cells and the drop-off rule are adapted from ProjectPunch's
  `ledge_tech.cpp` (MIT; see `THIRD_PARTY_NOTICES.md`).

### Frame advantage

`src/main/game/advantage.cpp` measures the frame advantage of an attack
that connected, on hit or on shield. It's adapted from ProjectPunch's
on-hit / on-shield advantage (`main.cpp` `resolveAttackTarget` /
`checkAttackTargetActionable` and `playerdata.cpp`
`resolvePlayerActionable` / `resolveTargetActionable`, MIT; see
`THIRD_PARTY_NOTICES.md`).

Kept from ProjectPunch:

- An exchange opens when a fighter's hitstun or shield stun is given.
- The result is the defender's frame minus the attacker's frame, each the
  first frame that side could act. "Could act" is the trackers' "actionable
  at any point this frame": ProjectPunch's "occupied" states, interrupt
  groups and the cancel window.
- The result waits until the defender has stayed actionable for more than
  the tolerance, and a new hit before then continues the exchange (stun
  refreshed the frame it ended, shield pressure).
- An attacker who could act and then hits again starts a new exchange.
- An advantage of 50 or more either way isn't shown.

Changed here:

- Who hit whom comes from the trackers' attribution (the fighter whose
  attack connected that frame) instead of "the first other player in slot
  order", and everything is indexed by tracker slot, not by player number,
  so any set of players and Ice Climbers' partners work.
- An attacker already free when its hit lands (a projectile's owner) is
  actionable on the hit frame. ProjectPunch never checked that frame and
  noticed free statuses only on a status change, so it showed nothing.
- Only a new hit re-arms the attacker, not a new attack that misses.
- Exchanges with no answer for `kGiveUp` frames are dropped.

### Colour overlays and hiding

`src/main/game/overlays.cpp` tints and hides fighters through the game's
own colour-blend and visibility modules.

**Colour** goes through `soColorBlendModule::setSubColor(colour, enabled)`
(header virtual; ProjectPunch's `color_overlay.cpp` uses the same call, MIT,
see `THIRD_PARTY_NOTICES.md`). The colour is kept until changed: the
module's `m_subColor` (`+0x14A`) and `m_useSubColor` (`+0x14F`) held a
colour written there for as long as observed, and the fighter is drawn
tinted by it. `make test-osds` (`scripts/osd_tests.py` `sc_overlay`: Mario
against a Fox CPU on Final Destination) checks those bytes and the green
pixels for the ProjectPunch preset's Wait colour, and that they clear when
it's off. That alpha 255 gives a solid colour was measured, setup not
recorded. Off is the state every fighter starts in: colour 0, not enabled.
Before the first call on a fighter, `isEnableSubColor()` must read the same
as `+0x14F`; otherwise the vtable isn't what the header says, and nothing is
done.

**Hiding** uses `soVisibilityModule::setWhole(0)` (header virtual, byte
`+0xE`, 1 normally) and `Fighter::setCursor(false)` /
`Fighter::setNameCursor(false)` (header virtuals, `Fighter+0x138` bits
`0x04` and `0x02`). `soVisibilityModuleSimple::update` (`0x80765990`) hides
the fighter model and synced effects when `isVisible()` is false, and
`soShadowModuleImpl::update` (`0x807C108C`) already exits before submitting
a shadow to `stShadow` when `isVisible()` is false (leaving
`soShadowModuleImpl+0x44` bit `0x02`, `isDispProjection`, at 0 while hidden
and back to 1 on the first shown frame without touching `activeStatus`).
On-screen, `Fighter::processFixCamera` (`0x8083A8B0`) re-evaluates
`setCursor` and `setNameCursor` every frame from `Flag_Cursor`
(`0x1200001B`), `Int_Cursor_Frame` (`0x10000020`) and `Flag_Name_Cursor`
(`0x1200001C`) without checking `isVisible()`, before
`stOperatorInfo::processEnd` (`0x80961318`) updates `IfPlayer`'s overhead
tag/arrow widget (which draws the CPU's "CPU" tag, a human's "P1" arrow and
name tags). An inline hook at `0x8083ABDC` (`sora_melee` `.text+0x1301C8`,
`src/stub/input_hook.cpp`) bails out before `tmPreservingCall` when no match
module is loaded and otherwise calls `game::overlaysFixCamera` right after
those updates to keep both cursors off for any fighter `overlays.cpp`
currently hides. Each fighter's pre-hide whole byte and cursor bits are
saved only when the fighter is visible and put back when unhiding, pausing,
transforming, saving/loading a savestate or detaching (never restored on a
`Dead`/`Standby` fighter). `getWhole()` must match `+0xE` first.

**Timing.** Overlays are applied before the game step, from the state of
the frame that ended, so an overlay shows one frame after the state that set
it.

**Tech invisibility.** Ground tech subactions (`Passive` `0xBE`,
`Passive_Stand_F` `0xBF`, `Passive_Stand_B` `0xC0`) use Project+'s
per-character first distinguishable frame (`kDistinguishable` in
`src/main/game/tech_frames.cpp`): hidden on 1-indexed
`stateFrame > dist + delay && stateFrame < kTechInvisEndFrame` (where
`kTechInvisEndFrame = 21` and `stateFrame = motionFrame + 2`, since
`motionFrame` is the 0-indexed frame the previous step ended on; `-1` disables
both invisibility and Tech Sound). Tech Sound (`src/main/cpu/options.cpp`)
fires on `stateFrame == dist` without Tech Invisibility Delay. Get-ups and
slip get-ups (`Down_*_U` `0xAF`-`0xB2`, `Down_*_D` `0xB8`-`0xBB`, `Slip_*`
`0xE6`-`0xE9`) and the `All Fighters` mode are adapted from ProjectPunch
(`invis_techs.cpp`, MIT): those get-up subactions use 0-indexed
`subactionFrame = motionFrame + 1` and hide when
`5 + delay < subactionFrame < 20` (`invis_techs.cpp:50-52`, `common.h:153`). Cursor hiding follows ProjectPunch's calls
(`Fighter::setCursor`, `Fighter::setNameCursor`) with save/restore ownership
and a single inline hook at `0x8083ABDC` (`src/stub/input_hook.cpp`) instead
of ProjectPunch's visibility-update and early-skip patches. `make test-osds`
`sc_tech` and `sc_invisible_overlay` check Fox's and Mario's whole-model
byte, shadow projection bit (`soShadowModuleImpl+0x44` bit `0x02`) and
cursor/name cursor bits, and `make test-cpu` (`tech-invis`,
`tech-invis-mario`, `tech-invis-sheik`) checks in-place and away techs,
get-ups, Tech Sound and mid-match Zelda-to-Sheik transformation through the lab
menu.

**Colour groups** follow the status the frame ended in, which is the one
that's drawn, so a status entered and left within one frame never tints.
Actionable is the exception, by design: it tints a frame in which the
fighter could act at any point (the trackers' actionable flag).

## The CPU

### Fighter values the CPU reads

The CPU Options read a fighter's state live, at the moment its controls are
read (`src/main/cpu/view.cpp`). Most of it is shared with the rest of the
mod: status, hitstun, shield stun, position and the work layout are
described in "Fighter values", and hitlag, situation and interrupt groups
in "Fighter trackers". The rest is read here:

- **Facing** is `soPostureModule` `+0x40`: `+1` facing right, `-1` facing
  left (observed; setup not recorded).
- **Shield health** is instance work float 3
  (`Fighter::Instance::Work::Float_Guard_Shield` in the header).
  `scripts/cpu_tests.py` checks it against the game while the CPU shields.
- **Knockback speed** is the kinetic module's damage energy (`+0x14` ->
  `+0x4C`, speed at `+0x8`/`+0xC`). That's where P+'s custom AI function for
  the knockback speed reads it (`ProjectM/CustomAI.asm`, "Custom AI
  Functions": `+0x14`, `+0x4C`, then `+0x8` and `+0xC`). P+'s
  `ProjectM/Ledge.asm:180-186` labels the same `+0x14`, `+0x4C`, `+0x8` path
  "Knockback X Speed", reached from the fighter's `+0xD8` -> `+0x7C`. It is
  set from the frame after the hit until hitlag ends, and decays after that
  (observed; character and move not recorded).

### Landing interrupts

Landing (status `0x16`, the `Landing_Heavy` animation) opens the action
interrupt groups once the motion frame reaches the fighter's landing frame:
the customize parameter `Landing_Frame`
(`ftValueAccesser::Customize_Param_Float_Landing_Frame` = 3074 in the
decompilation's `ft_value_accesser.h`), read with
`soValueAccesser::getConstantFloat`, unless that parameter is -1.
`LandingLight` (`0x17`) does the same with parameter 3073.
This is the game's own rule, read in its code: the landing code at
sora_melee `0x808746B8` calls `getConstantFloat` for 3074 at `0x8087472C`
and for 3073 at `0x808747A0`, and opens the groups with work flag
`0x22000010`.

It matches what was observed after a crouch-cancelled hit, with the
animation playing at speed 1. Fox (parameter 3) and Ice Climbers (3) have
the groups closed at animation frames 0-3 and open at frame 4; Bowser (4)
opens at frame 5. A guard, jump or attack pressed in the frame that takes
the animation past the parameter comes out in that frame: the controls
are acted on in that same frame, after the animation has advanced.

Three other values look like they could be the landing frame but aren't:
the heavy landing frame parameter (Ice Climbers' is 7), instance work
float 0 (`Float_Landing_Frame`, 0 there), and the subaction's script (Ice
Climbers' is empty).

### Analog trigger variables

P+'s "Analog C-Stick, L R, & Light-Shield Button Stored as Variables"
(`Source/ProjectM/ButtonPresses.asm`, a hook in `aiStat`'s update) reads
both analog triggers of the fighter's controller every frame, straight from
the port's system pad. It stores them in instance work floats 36 (L) and 37
(R), from 0 to 1, and the frame before's in floats 40 and 41. Instance work
int 77 says whether a trigger at 0.3 or more is new (1), held (2) or absent
(0). That's what the code at `ButtonPresses.asm:433-457` writes; the header
comment at its line 211 has 1 and 2 the other way round. The fighters'
scripts read these values for the light shield.

When the lab takes one trigger out of a fighter's input
(`hideAnalogTrigger`), it clears that trigger's floats and works int 77 out
again from the other trigger alone, the same way P+'s code does.

### Smash DI suppression

A CPU that DIs a hit pushes its stick fresh on the last hitlag frame, and a
fresh push in hitlag triggers a smash DI. To prevent unintended SDI during
pure TDI, `holdStickStill` makes the stick count as held rather than flicked
on that frame.

P+ smash DIs when the stick is out 0.7 in hitlag and its X or Y flick
counter is under 4 (parameter 23041, `Hit_Stop_Delay_Flick_Frame` in
`ft_value_accesser.h`). `soControllerImpl::update` (`0x80764DD0`) first copies
the stick to its previous stick (`+0x10`/`+0x14`). Then, after the
fighter-input call the mod runs in, it reads the new stick and sets an axis's
flick counter (`+122` X, `+124` Y) to 0 when the axis left 0.35 or changed
side against the previous stick; otherwise it counts the counter up. So the
mod sets the previous stick to this frame's stick and the counters to at least
`kHeldFlick` (6), safely past Brawl's 4-frame flick window.

It has to be called from the fighter-input callback, for the fighter whose
input is being read (`sub` selects Nana). It returns false if the controller
isn't there.

### Fastfall conditions

The game's fastfall is general term 10006, run by the aerial subroutine
(Fighter.pac; not checked here). It needs all of these:

- the stick's y at -0.665 or lower;
- the stick crossed -0.35 downward within the last 4 frames;
- the summed vertical speed below 0;
- in tumble, hitstun over.

`DamageFall` never runs it. So a fighter whose tumble turns into
`DamageFall` at the top of its fall (Jigglypuff) can fastfall only on its
last `DamageFly` frame. That was observed: a flick as Jigglypuff's speed
went from 0.001 to -0.034 there fastfalled, and flicks in `DamageFall` never
did.

The CPU's "until falling" wait (`kUntilFalling` in
`src/main/cpu/options.cpp`) ends when the speed after this frame's gravity
is below 0, that is, when the fighter is falling in the coming frame. The
flicks that come after the wait keep the downward crossing fresh.

### When P+ reads ASDI and trajectory DI

P+ reads the automatic smash DI from the stick on the last hitlag frame, and
the trajectory DI on the frame after it, when hitlag ends. A push on one of
the two frames either moves the fighter or turns the launch, not both
(observed; character not recorded). So the CPU's hitlag script puts the ASDI
direction on the first of those frames (the trajectory DI's direction with
Auto) and the trajectory DI on the second. P+ would take ASDI from the
C-stick first, but it reads a CPU's C-stick from no controller.

That last hitlag frame's stick is held still for the controller (see "Smash
DI suppression"). Pushed in fresh, it would also smash DI, 6 units on every
hit the CPU DIs.

### CPU tech timing

A CPU set to tech presses guard just before it lands
(`techInput` in `src/main/cpu/options.cpp`). The rules it follows:

- **The tech window** is the landing frame and the 19 before it (not
  checked in the game's code or Fighter.pac).
- **A guard press techs only if it comes 41 frames or more after the guard
  press before it** (`kTechGap`). Any press counts: shield, air dodge, or a
  tech, the CPU's own included. These are Fighter.pac's tech terms with
  parameters 23052 and 23053 (`Passive_Trigger_Frame` and
  `No_Rapid_Frame_Value` in `ft_value_accesser.h`); the terms and values are
  not checked here.
- **When guard is pressed.** It's pressed when the landing is predicted this
  many frames ahead or fewer: `kTechLead` (5) for Tech Lockout's Latest Tech
  Input, which leaves a margin for the prediction, and `kTechLeadEarly` (15)
  for Earliest. The press then comes 4 and 15 frames before the landing
  (measured; character not recorded).
- **Simulate Tech Trap** treats a hit in tumble as if the CPU were a player
  who pressed guard too early for the next landing. Latest presses at once
  (the game refuses a tech press for 40 frames). Earliest makes no tech press
  until the frame a press `kTrapEarly` (24) frames before the hit would allow.

The landing is predicted along the fighter's path with the game's collision
check, platforms and slopes included; without ground found, the height the
fighter last stood at is used. A few details matter:

- The fighter lands when the bottom of its collision shape meets the floor.
  In tumble that point is a few units above its position (2.3 to 4.4;
  observed, character not recorded), so the ray is cast from there: the
  position itself goes below the floor it lands on.
- A fighter in `Damage_Air` sinks below the floor and stays airborne until
  its hitstun is over (observed: Bowser 3.9 under it). A floor the fighter
  is already under doesn't count as a landing.
- At the fall speed cap the speed no longer grows. Without that, a tech
  roll's stick came a frame early and gave a wiggle instead (observed;
  setup not recorded).
- In a damage status the fall is followed as the DI Display follows a
  flight: the knockback loses the decay (parameter 3294) each frame, and the
  gravity speed grows up to the largest fall speed. The knockback's own
  slowing makes a fall 15 frames ahead (Tech Lockout Earliest) land up to 12
  frames later than the change over the last frame says.

The roll's direction is the stick on landing: `|x|` of the roll parameter
(3198, 0.2) or more (`Fighter.pac` `Term_Passive_Fb` `0x80FAB024`, direction
read at `Passive_Fb` entry `0x80FB563C`). Once hitstun is over, a push of the
wiggle parameter (3299, 0.75) or more that left the 0.35 dead zone on this
frame or the one before ends the tumble instead (`Fall`, nothing left to tech),
and holding any non-zero horizontal stick during `DamageFall` drifts the CPU in
the air. So the CPU keeps the stick neutral until within 1 frame of predicted
landing (`kTechRollLead`), then applies a magnitude midway between the roll and
wiggle parameters (`tech_logic.h`).

Banana trips (`Slip` `0x89` and `Slip_Down` `0x8A`) are grounded techable
statuses in P+ (`Source/Injects/MoveDef/Items.asm:10-39`, `0x80545ED8`):
`Passive` (`0x60`) and `Passive_Fb` (`0x61`) unlock once animation frame
`IC_Basic[0] >= 20.0` with the same 20f guard window, 40f lockout, and 0.2 roll
stick check. On a missed slip tech, `Slip_Wait` (`0x8D`, `0x80FB755C`) uses the
same get-up inputs as `Down_Wait` (`0x4D`).

### Ending the match from the lab

The lab menu's Return to CSS row ends the match the way Training's own Finish
ends it (`src/main/game/match.cpp`), so the game's code takes it cleanly back
to Training's character select. Character RNG choices are lab settings
(`settings.h`, `characterRng`) kept across matches like the others.

Everything a match end must undo runs at the module's detach, on the scene
exit, whichever way the match ended. The settings save started by
`closeMenu` is finished there (storage), savestate and recording memory is
freed, and the observers come off.

### The CPU's damage after Training's pause

Training's pause rewrites the CPU's damage when it closes, which would undo
damage set or accumulated in the lab, so `src/main/game/training_damage.cpp`
takes Training's write out while CPU Damage was not edited in Training's pause.

This is what the game does, read in sora_melee as Project+ 3.2.1 runs it.
`stOperatorInfoTraining::processBegin` (`0x809620CC`), on the step
Training's pause closes, clears the pause byte (`+0x128`) and then loops
over the fighters. P+'s hook at `0x8096256C` ("Damage Stales in Training
Mode, pause resets") clears each one's stale-move queue and skips the
player. For each CPU, the game then calls:

- `ftOwner::setDamage(CPU Damage, false)` (the `bctrl` at `0x80962594`,
  vtable `+0x10`);
- `ftOwner::setStartDamage(CPU Damage)` (`0x809625BC`, `+0x18`);
- at `0x809625EC`, `bl 0x800E14A4`, the HUD's damage setter (IfPlayer,
  found as `IfMngr+0x4C[IfMngr+0x68[player]]`). It stores the value at
  `+0x18` and draws the digits from it; a hit calls it too, with the new
  damage.

CPU Damage is the pause menu's value (`IfMngr+0x78` -> IfMinigameTraining
`+0x28`, 0..999), read by IfMngr's getter `0x800DCE3C` and stepped by
`curPosProcCPDamage` (`0x80105C1C`). Measured before the change
(characters and stage not recorded): 4% -> 0% after a pause with CPU Damage
0; with CPU Damage 3, 7% -> 3%.

So while CPU Damage is what it was when the pause opened, the three calls
are taken out of the code. The stale queues are still cleared, and the CPU
type calls after them still run. If the player changed CPU Damage in the
pause, the game's write stays, so Training's own option still works when
it's used. The HUD call has to go too: without that, the CPU kept its damage
but the HUD read CPU Damage (0% after setting CPU Percent 37 through the
pause).

### Display options

The Display Options and a few General rows (Game Speed, Move Staling,
Camera Mode) change the game directly. Everything below was read in the
game's code or tried in the emulator on RSBE01 rev 2 with Project+ 3.2.1,
and `scripts/display_options_tests.py` checks it. The code is in
`src/main/display/display.cpp`.

**Scene layers.** `gfSceneRoot::setLayerDispStatus(root, layer, on)`
(`0x8000D234`) sets bit `0x80` of the root's byte `+0x399 + 0x18 * layer`;
the root is `g_gfSceneRoot`. Layer 8 holds the HUD (damage, names) and
Training's pause menu. Layer 0 holds the stage and its background, and
layers 4 and 5 hold most effects: dust and trails, but not the large hit
flash, which none of the layers 4-7 holds. Project+'s Code Menu uses the
same calls for its HUD line and its "Models Off".

A hidden layer stays hidden across scenes. So every layer the lab hides is
shown again when the option is turned on, when the module detaches, and (the
HUD layer only) while Training's pause menu is open, which it would
otherwise hide. Hidden layers are set again every frame. A layer is only
ever shown by this code if this code hid it, so the Code Menu's own HUD line
keeps working. The layer bits are in the memory a savestate copies, so a
load brings back the bits of the moment it was saved; after a load, which
layers this code holds as hidden is read back from the bits (seen in game:
a state saved with HUD off, loaded with HUD on, kept the HUD hidden before
this).

**Move Staling off.** Each fighter owner's stale queue is a
`ftLogPatternModule` at owner data `+0xCC8`, the queue Project+'s Training
code clears through its vtable `+0x28`. With Move Staling off it's emptied
before every game step with `ftLogPatternModule::clearPattern`
(`0x808E01E4`). With Project+'s ratio table an empty queue gives full
damage: a jab does 3.0 every time.

**Game Speed.** This is `gfApplication`'s frame rate, the u16 at `+0xF8`.
The game runs rate / 60 game steps per rendered frame and skips whole
steps: 50 runs 5 of every 6, 40 runs two of 3, 30 one of 2 and 15 one of 4
(stepped in the emulator). Training's own Speed option writes the same
field when it changes (x1.5 is 90, x1 is 60, 2/3 is 40, x2 is 120) and
never rewrites it otherwise. Its 1/2 and 1/4 keep 60 and slow the game down
through the game's slow rate instead.

A Game Speed other than 1 overrides Training's rate. The rate found there
(Training's) is put back when Game Speed is 1 again, while Training's pause
menu or the lab menu is open, and at the end of the match. Training's 1/2
and 1/4 slow motion still applies on top. Step counters (the trackers, OSD
timers, frame advance) count game steps, so they stay right at every
speed.

Below 60 there's one catch. The system pads' "pressed" bits are still made
once per main-loop pass, so a press that starts on a pass without a game
step is never seen as pressed by a step (observed: half of Training's Start
presses were lost at 1/2, and also at Training's own 2/3). So each press is
kept marked as pressed on the system pads until a game step has run.

**Camera.** `CameraController::getInstance` (`0x8009BE74`) gives the
camera; the kind it runs is at `+0x40`: 2 in a match, `0xD` while
Training's pause menu is open or its Camera option is Zoom. Training's
Camera option (its menu object `+0x2C`: 0 Normal, 1 Zoom) is applied by
`IfMinigameTraining::setCameraType(menu, 0 or 2)` (`0x8010535C`). 2
changes to kind `0xD` on the player. 0 goes through Project+'s hook to
kind 2, or to `0xC` on stages it marks as fixed-camera
(`Project+/StageFiles.asm`, hook at `0x80105400`: the stage file's flag
`0x2`).

The lab's camera modes work like this:

- Zoom sets Training's option and calls the same function, so Training
  keeps it after its pause.
- Fixed is `CameraController::changeInput(0xC)` (`0x8009C69C`,
  MeleeFixed: the whole stage), set again whenever the kind is something
  else outside Training's pause.
- Normal leaves the camera to Training.

### Brawl's match timer

The lab drives Brawl's own match timer (top centre) from its centre HUD object
(`IfMngr+0x44`). The code is `src/main/ui/match_hud.cpp`;
`scripts/match_hud_tests.py` checks it. All addresses are RSBE01 rev 2 as
Project+ 3.2.1 runs it.

**The timer** ("MM:SS ff") is made for every match (`0x800DD730-0x800DD754`,
the model at `+0x154`) but only shows while bit `0x80` of the centre's
first byte is set. The centre's start code sets it only when it is passed
a timer (`0x800DDA84-0x800DDA90`); in a Training match it was clear (seen
in game). So the timer is turned on by setting `0x80` and `0x40` and calling the timer's setup (`0x800DDB54`,
which allocates nothing), then the set-time routine (`0x800DDCF8`, centre and
frames) on every render call. `0x40` keeps the last five seconds from
switching to the big countdown digits (`0x800DDD34`). The set-time routine
stores the value at `+0x17C` and puts the model in the HUD's scene group
itself. Its small digits are frames, 00 to 59, not hundredths
(`0x800DDF7C`): 4540 frames shows `01:15 40`. Turning it off is what the
set-time routine does before the countdown: clear the bits and take the
model out of the group at `+0xC` (or `+0x8` when that is empty) through the
group's vtable `+0x3C` (`0x800DDD68-0x800DDDAC`).

**Asking for it.** A caller asks for the timer on every render call it should
show (for example the lab timer). After the render hooks have run,
`matchHudApply` shows, sets or hides the timer, so anything that stops asking
(HUD off, Training's pause, a menu) takes it off the screen on the next
picture. It is only driven in a Training match (`IfMngr+0x4` is 5) and only if
the centre's `0x80` was clear when we first saw that centre, so a match with
its own timer keeps it. That first look is also what decides ownership across
savestates: loading puts the centre's flags and its scene group back as they
were saved (all in the savestate's region set), and the next render call brings
them to what is asked for now.

While the lab menu is open over a running match (Menu Open = Start), the lab
hides the HUD layer (see "Display options"), and Brawl's timer goes with it.

**One player's damage box** can be hidden while the rest of the HUD layer
stays (timer remains visible). Each HUD slot has an `IfPlayer`
at `IfMngr+0x4C`; its models are the parts at `+0x28` (50 slots, each part's
model at `+0x10`). Setting bit `0x10` of the model's `nw4r::g3d::ScnObj`
option flags (`+0xCC`, read by `0x801AAE44` as the gather option) skips
gathering that slot's portrait and percent without calling `disappear`. The
flag is refreshed every render call while requested and cleared once nobody
asks.

### Hitbox and collision drawing

Project+ 3.2.1 replaces the empty `renderDebug` virtuals of the collision
modules with drawing code. These are Gecko C2 codes by Eon: a branch at the
function's first word to a body in the code handler's area. Project+ runs
them from a debug pass of the task scheduler while its Code Menu's Hitbox
Display or Stage Collisions flags are set. That pass doesn't run while the
game is stopped (frame advance), and the Code Menu rewrites the flags, so
the lab calls the same routines itself, from the render hook, for the
fighters it wants. The code is in `src/main/display/debug_draw.cpp`.

- Hurtboxes: `soCollisionHitModuleImpl::renderDebug` (`0x80750F58`), the
  module at `soModuleEnumeration` `+0x20`, vtable slot `+0xD8`.
- Hitboxes: `soCollisionAttackModuleImpl::renderDebug` (`0x8074BDB0`),
  module `+0x1C`, slot `+0x120`. Its body passes `r31` on without setting
  it: that's the caller's, and P+'s StageObject routine holds the module
  accesser there. It skips every hitbox if the word `r31` points at is 1.
  So the lab calls it with `r31` set to the fighter's module accesser, as
  P+ calls it.
- Shields, reflectors and absorbers: modules `+0x24`, `+0x28` and `+0x2C`,
  slot `+0x88`. These are vanilla routines that P+ recolours (hook at
  `0x807513F4`).
- ECB: the ground module (`+0x10`; its vtable at its `+8`), slot `+0x21C`,
  which P+ patches to draw filled diamonds (`0x80137850`, `0x80137860`...).
  These patches aren't Eon's alone: the code's title in Project+'s source,
  "Collision Debug Function" in `Source/Project+/Debug/soGroundModule.asm`,
  names Arthur and Eon.
- Stage collision: P+'s list walk (its `renderDebug` for
  `grCollisionManager`) reads the list of collision entries at
  `0x8049E57C` and draws each entry in use (flag `0x80` of the byte before
  the link) through its helper at `0x80541F90`. The lab repeats the walk,
  without its flag checks.

Every entry point is checked against the instruction words read on this
setup before anything is called (other Project+ builds may differ). A
mismatch turns the option off with a log line.

The colours come from Project+'s `data.debug`, loaded at `0x80548400` (it's
loaded once the byte at `0x805483FF` is 1). Hitbox id n uses the word at
`+0x64 + 4n` (ids 8 and up share the last); ids 0-7 are red in 3.2.1 and
the last word is 0, so P+ draws ids 8 and up clear.

## Savestates and recordings

### Savestate preconditions

A save or a load is checked at the top of the main loop, right before it
would run. A request that fails any of these checks is refused, and nothing
changes:

- A running Training match that isn't paused. Training's pause holds the
  game's tasks with flags that live inside the state (not checked in the
  game's code), and the menus own the D-pad then anyway.
- Exactly two fighter entries, and Training's No. of CPUs at 1 (the
  `ftManager` entry count, and `IfMinigameTraining` `+0x20`). The slot lives
  in the heaps the third and fourth fighters' files would use.
- No file being loaded: both of `gfFileIOManager`'s request queues are
  empty.
- Every entry on its first instance, and no fighter in its Final Smash
  status. Transformations and Final Smashes load and swap fighters.
- None of our own transitions in progress (the lab menu, Start during frame
  advance, the camera catch-up after a pause), and no Project+ debug pause
  pending.
- For a load: a slot saved in this match (same stage and players), with the
  heaps and modules where they were, the same files loaded, and the slot
  unchanged since the save. That check covers every copied range's address
  and length, which the load writes memory with, and one word in 64 of the
  data; a full pass over the slot took longer than the copy itself.

### Our own state across a savestate load

The fighter trackers take their observers off before every copy, so a slot
never holds them, and put them back afterwards. On a load they also forget
their history and start again from the restored frame. Frame advantage
exchanges, OSD messages and the info display's status clock are cleared.
Frame advance, the lab's settings and the percent locks are kept, and the
stop bit is put back the way frame advance wants it. The menu is closed
whenever a save or load can run.

### Savestate feedback

A save plays `game::kSoundConfirm` and briefly flashes a light tint across
the screen (omitted during frame advance). A load plays `game::kSoundBack`.
A refused save or load plays `game::kSoundError`, and structural refusals
(more than one CPU, transformed fighter, no room beside a recording's state,
file loaded since save) also post a red OSD message.

### The state at the lab's start

The lab takes a silent state on its first running frame when the slot holds
none, so D-pad left has one to load before any save. It is tried again every
frame while a transient refusal holds (a file still loading), and given up
after any structural refusal. While that initial state is the one held,
clearing it (No. of CPUs raised, or the recording's state needing its room)
shows no message.

A D-pad load with a recording playing returns to the frame the state was
taken on and re-rolls a Random recording slot.

### The savestate's region set

The region set is what a Training match's state is made of in memory. It's
found at save time by walking the game's own heap table and module list,
plus some fixed ranges. The rules come from memory dumps, diffs and pointer
checks of Training matches on RSBE01 rev 2 with Project+ 3.2.1, and each
class below is what those runs showed:

- Gameplay heaps: the pool object, every allocated block whole, and the
  header of every free block (the free list), so the heap's own bookkeeping
  comes back with its contents.
- The fighter instance heaps in use: the whole range, free space included,
  so blocks allocated after a load start with the bytes they had the first
  time. Without this, replays after a load matched in every gameplay value
  but not byte for byte.
- Resource heaps: copied like gameplay heaps, except that blocks holding a
  loaded file (an archive, which starts with `"ARC\0"`) are left out. Those
  don't change during a match, while the other blocks (the model instances
  built from them) do. The archives' places and sizes are kept as a
  signature instead, and a load is refused if any file was loaded or freed
  since.
- The `.data` and `.bss` sections of every loaded REL module except ours.
- Fixed ranges in the DOL's data and in Project+'s own areas.

Never copied: sound, pads, threads, the OS, file IO, the GPU FIFO, the frame
buffers, our own modules, and the slot itself.

### The main loop's step budget and the rewind burst

`gfApplication::mainLoop` (`0x80016EE0`; its loop is `fn_80016F8C`, which
it calls at `0x80016F74`; read in code on this build) gives each main-loop
pass its game steps after the system pads. Its step budget (an `s32` at
`+0xFC`) loses the frame rate (`+0xF8`, 60 at normal speed) times the frames
passed (1, or up to 3 when it's behind). Each step then adds 60 until the
budget is above 0 again, and the frame is drawn once after them. That's how
Game Speed x2 runs two steps a frame.

The rewind uses this to replay many steps without drawing them: a budget of
`1 - 60 (k - 1)`, set by the recording code, runs `k` steps in that pass.
Steps past the replay's frame are held by frame advance (`rewindStep`), so
`k` doesn't need to be exact.

The frames passed come from `gfPadSystem`'s count of video frames since the
last pass (`+0x30`; `consumeFrameCounter`, `0x800293DC`, takes at most 3 a
pass). A long replay pass lets that count pile up, and the game would then
catch up with 3 steps a pass for a while. So it's set to 0, as the main
loop's own resets do, in every replay pass and the pass after.

### Putting a fighter back at its saved position

The lite restore puts each fighter back with its status first, then the
warp, then the ground module. That order was found to work from the ground,
from a run, from a shield and from the air:

- The status module refuses a ground status while the fighter's situation
  is the air (read in `changeStatus`), so Wait only takes for a fighter on
  the ground. The warp asks for Fall (read in `Fighter::warp`), and a
  fighter from the air lands out of that.
- `attachGround` runs the collision check where the fighter now is, puts it
  on the line under it and attaches it to that line. It returns false when
  there's no ground there (a position saved in the air), and the fighter
  falls. Without this call, the fighter fell for a frame and landed: at once
  on a solid floor, but through a pass-through platform it had been put
  exactly on (measured on Battlefield's three platforms).
- `setStGroundForce` makes the fighter count as standing. Without it, a
  fighter warped from the air lands with landing lag.
- `clearSpeedAll` leaves the last hit's launch (the damage energy) alone,
  and that doesn't decay while the fighter stands: a CPU put back six frames
  into a forward smash's flight slid on at 5 units a frame and left the
  ground again (measured; characters not recorded). So the launch is cleared
  too.

A fighter put down out of a tumble stays in Landing for some 30 frames
before it stands in Wait (measured; character not recorded).

## The lab menu

### Where the info display's values come from

Most info display rows come from the trackers. These are read straight from
the game instead (RSBE01 rev 2, Project+ 3.2.1). "Read" means read in the
game's code; all of them are checked against memory by
`scripts/lab_tests.py`. The code is in `src/main/ui/info_display.cpp`.

- Self and KB velocity: the kinetic module's `getSumSpeed` with the
  attribute mask 1 (the motion, gravity, drift and stop energies) and 2
  (the damage energy). The game's own code passes the same masks.
- Sticks and trigger, System: the game pads `gfPadSystem::updateGame` fills
  (`+0x444`) for the fighter's port. The main stick is shown at Project+'s
  scale, as `IpHuman` makes it before the 0.28 dead zone
  (`src/shared/game/stick.h`: capped at 100 units, times 0.010325, so 1.0
  at about 97 units). The C-stick is shown at Project+'s analogue C-stick
  scale (times 0.011775, clamped: full at about 85 units). Triggers cover
  their whole range (255 = 1.0).
- Engine: `soControllerImpl`, which the fighter's code reads: the main
  stick, and the trigger (`+0x38`), which is the larger analog trigger the
  input gives, set to 1.0 while the shield button is held (read at
  `0x80764EB0`-`0x80764F5C`). Its C-stick (`+0x20`/`+0x24`) is always 0,
  because the update stores 0.0 there (`0x80764ED0`), so that value isn't
  offered.
- Ledgegrab Timer: LA-Basic[2], set to param 23050 (29) on leaving the
  ledge and counted down by the fighter's per-frame update (read).
- Intangibility: the body hit group's intangible (`+0x24`) and invincible
  (`+0x28`) frames left (see "Fighter body values"). A move's own script
  sets the state without a count; that's shown as "move".
- Shield Health: LA-Float[3] (see "Fighter values the CPU reads").
- Grip Strength: the held fighter's grab timer (`getClatterTime(0)`).
- ECB Lock: Project+'s rule (`Memory.asm`, hook `0x8073A2A4`). The shape's
  bottom is the fighter's position while its frames-airborne count (`+0x10`
  of Project+'s extra block; see "Fighter body values") is 1-9, or while
  its platform-drop count (`+0x18`) is above 0.
- ECB Bottom: the ground module's `getDownPos(0)` less the fighter's
  position (an offset from the fighter's position). The game copies the point
  from the collision status's current shape (`+0x60`, read at `0x801341d8`).
- Jumps: LA-Basic[1] against param 23003 (`Jump_Count_Max`).
- Walljumps: LA-Basic[3] (`Int_Wall_Jump_Count`), which the wall jump reads
  with param 3224 (`0x8086e4a4`). Landing sets it back to 0 (seen on
  Mario).
- Can Walljump: per-character constant IC-Basic[23189] and airborne cooldown
  LA-Basic[4] (`Int_Wall_Jump_Past_Frame`, 20f cooldown IC-Basic[23069];
  formatted by `src/main/ui/wall_info_core.h`).
- Wall Cling: per-character constant IC-Basic[23190] plus airborne usage
  state LA-Basic[12] (`Int_Attach_Wall_Count`) / LA-Basic[11]
  (`Int_No_Attach_Wall_Frame`), with Knuckles (`0x2D`) reported as `Climb`.
- Line Info: for the ground, ceiling, left and right wall, the ground
  module's `isTouch` with that side's mask and the touched line's id
  (`getDownLineId` and the others; the line works at `+0xD4` of the
  collision status, 16 bytes per side), or -1 when that side touches nothing.
- Blastzones: `CameraController` `+0x158`/`+0x15C` (left, right) and
  `+0x160`/`+0x164` (top, bottom), the values `soGroundUtil::checkDeadArea`
  compares with.

### Adding a menu row

Each lab menu page lives in its own file in `src/main/ui/pages/`, and
`registry.cpp` lists the pages in `PageId` order.

To add a row, add it to the page's table and allocate its value in the
matching feature area of `main::Settings` (`settings.h`), taking bytes from
that area's `reserved` array so existing settings areas stay compatible. Its
default is the row's `def`.

A row that is conditionally available at run time (for example recording
slot rows before positions are saved) takes an `enabled` callback through
`TM_ROW`. While it returns false, the row renders disabled and the cursor
skips it.
