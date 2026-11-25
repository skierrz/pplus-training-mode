#!/usr/bin/env python3
"""Generate BPE-compressed src/extra/menu_descriptions.cpp from readable tables.

Usage:
  python scripts/gen_menu_descriptions.py          # regenerate menu_descriptions.cpp
  python scripts/gen_menu_descriptions.py --check  # verify file is up to date
"""
import pathlib
import sys

DESC_TRAINING_MENU = "Switch directly to Brawl's built-in Training pause menu."
DESC_EXIT_CSS = "End this session and return to the Character Select Screen."
DESC_INFINITE_SHIELD = "Off (normal), Until Hit (full until struck), or On (always full)."
DESC_REV_ACTION = "Secondary reversal action mixed with the primary trigger option."
DESC_REV_DELAY = "Actionable frame delay before the Slot 2 reversal fires."
DESC_REV_CHANCE = "Probability weight for Slot 2 versus primary Slot 1."

TABLES = {
    "kGeneralDesc": [
        "Freeze gameplay and step one frame per advance press.",
        "Set Player 1 starting damage percentage immediately.",
        "Keep Player 1 locked at the chosen damage percentage.",
        "Add up to this much random P1 damage on each savestate load.",
        DESC_INFINITE_SHIELD,
        "Run match gameplay at full speed or slow-motion ratios.",
        "Switch between normal match camera, zoomed, or fixed view.",
        "Toggle P+ stale-move queue tracking for attack damage.",
        "Show read-only OSDs, Info, Ledgedash HUD, and PSA in Offline VS.",
        "Configure frame advance, D-pad bindings, menu button, and style.",
        "Restore all Training Lab settings to default values.",
        DESC_TRAINING_MENU,
        DESC_EXIT_CSS,
    ],
    "kScenariosDesc": [
        "Load a preconfigured P+ practice setup with auto-warping.",
        "Automatically reset fighters when the current drill completes.",
        "Choose whether the scenario spawns on the right or left side.",
        "Pick the CPU multi-hit attack for the Multi-Hit SDI preset.",
        "Immediately warp fighters back to the active scenario start.",
        "Configure hitstun auto-reset, recovery looping, and 3D meters.",
    ],
    "kCpuDesc": [
        "Set the CPU fighter's current damage percentage.",
        "Hold the CPU at its configured damage after every hit.",
        "Add up to this much random CPU damage on each savestate load.",
        "Use Training Lab AI controls or Brawl's native Training CPU.",
        "Set what the CPU does while standing free in neutral.",
        "Configure CPU trajectory DI, hitlag smash DI, and ASDI.",
        "Configure ground/air/shield counters, reversals, and SHFFL.",
        "Configure tech rolls, getups, traps, and tech invisibility.",
        "Configure CPU shield regen, tilt, no-knockback, and mash.",
        "Select native Game AI, PPlusAI offstage recovery, or Off.",
        "Choose how the CPU gets up after grabbing a stage ledge.",
        "Reposition the CPU fighter directly on stage with your stick.",
        "Hold the CPU frozen in place without advancing its state.",
        "Route another controller port directly into the CPU fighter.",
    ],
    "kRecordingDesc": [
        "Save current fighter states or warp back to saved positions.",
        "Set Player 1 to Off, Record, Playback, or Re-Record inputs.",
        "Choose which P1 input slot records or plays back (or Random).",
        "Control, record, or play back input slots on the CPU fighter.",
        "Choose which CPU input slot records or plays back (or Random).",
        "Flip playback left/right (locks P1/CPU mode and slot rows).",
        "Set when the CPU switches from playback back to counter AI.",
        "Restart saved positions and playback automatically when done.",
        "Restore saved positions after playback ends or CPU counters.",
        "Hold playback frozen on restore until any live controller input.",
        "Let P1 or CPU interrupt playback by pressing controller inputs.",
        "Wait to begin recording until the first non-neutral input.",
        "Overwrite the saved recording positions with current states.",
        "Trim frames before the current playback frame and re-save start.",
        "Press A twice to clear saved positions and recording slots.",
        "Inspect, edit frame-by-frame, copy, or delete recorded slots.",
        "Set random playback weights and bonus damage for P1 slots.",
        "Set random playback weights and bonus damage for CPU slots.",
        "Save the current setup and recorded input slots to the SD card.",
    ],
    "kDisplayDesc": [
        "Show or hide Brawl's match timer, damage percents, and tags.",
        "Show live multi-controller sticks, triggers, and buttons.",
        "Customize Menu Accent, OSD, and Info Display colors and opacity.",
        "Draw active attack hitboxes and hurtboxes on P1, CPU, or Both.",
        "Color active hitboxes by property or priority ID.",
        "Accumulate persistent attack coverage overlays across frames.",
        "Draw the outer reach bounding box around the hitbox heatmap.",
        "Adjust transparency of the accumulated hitbox heatmap overlay.",
        "Set how many active frames span each heatmap color band.",
        "Visualize active grabboxes, tether lines, and item catch zones.",
        "Draw real-time launch trajectory, TDI, SDI, and ASDI vectors.",
        "Preview preset DI angles on the live DI compass and arc.",
        "Draw stage floor, wall, ceiling, and ledge collision geometry.",
        "Show or hide 3D character models for P1, CPU, or Both.",
        "Toggle rendering of the 3D stage background and geometry.",
        "Show or hide hit sparks, smoke, and visual particle effects.",
        "Color-code P1 fighter states (lower rows override upper rows).",
        "Color-code CPU fighter states (lower rows override upper rows).",
        "Configure live frame and physics telemetry rows for Player 1.",
        "Configure live frame and physics telemetry rows for the CPU.",
        "Show the ledgedash frame timeline bar and GALINT readout.",
        "Toggle 3D floating state meters above fighters anytime.",
        "Open PSA AnimCmd script inspector and frame debug overlay.",
    ],
    "kOsdDesc": [
        "Apply a 1-click competitive OSD bundle (Custom, Essentials, etc).",
        "Choose where OSDs stack (Sides uses left/right edges in 1v1).",
        "Limit how many notifications display at once per player.",
        "3D timing bar above fighters (independent of text OSD toggles).",
        "Post on-screen callouts for 44-fighter P+ signature techs.",
        "Show wavedash timing, airdodge angle, and short/full hop.",
        "Report L-cancel frame timing, misses, and session success rate.",
        "Report ledge hang duration, GALINT frames, and grab limit.",
        "Display the first frame you act out of shield release.",
        "Display measured frame advantage on shield after hitting guard.",
        "Display measured frame advantage after landing a clean hit.",
        "Allow extra idle frames when measuring frame advantage.",
        "Report successful, slow, or missed dashback turnarounds.",
        "Show powershield timing or how early a missed press occurred.",
        "Report how quickly you act after landing lag or autocancels.",
        "Show frame timing for aerials, specials, and double jumps.",
        "Show how soon you acted after leaving hitstun or tumble.",
        "Report timing frames for jump-canceled grabs and up-smashes.",
        "Show how many frames after apex you triggered a fastfall.",
        "Show execution frame timing for boost grabs and DACUS cancels.",
        "Count smash DI shifts completed during each hitlag window.",
        "Alert when an early tech press triggers the tech lockout timer.",
        "Show hit count and total damage when a true combo finishes.",
        "Display grab breakout timer and mash rate per frame.",
        "Display timing frames for glide tosses and aerial item throws.",
    ],
    "kStageCastDesc": [
        "Swap P1 or CPU fighters and costumes mid-match without CSS.",
        "Expand all stage and fighter rows even when not in this match.",
        "Override dynamic stage hazards to Default, On, or Off.",
        "Cycle, freeze, or lock moving stage platforms to a preset.",
        "Set the Fountain of Dreams left platform to a fixed height.",
        "Set the Fountain of Dreams right platform to a fixed height.",
        "Force Peach vegetable pulls to a specific turnip face or item.",
        "Lock Peach forward smash to Golf Club, Pan, or Racket.",
        "Keep Peach float available without needing to re-land.",
        "Force Luigi Green Missile to misfire Always, Never, or Default.",
        "Force Mr. Game & Watch Judge hammer to a fixed number (1-9).",
        "Lock Mr. Game & Watch Oil Panic bucket fill level.",
        "Hold Samus Charge Shot at Empty, Half, or Full charge.",
        "Lock Samus active beam mode to Plasma or Ice.",
        "Allow unlimited Samus aerial bombs without landing resets.",
        "Hold Mewtwo Shadow Ball at Empty, Half, or Full charge.",
        "Grant Mewtwo unlimited hover duration without landing resets.",
        "Hold Lucario stored Aura charges at 0, 1, or 2.",
        "Hold Lucario Aura Sphere at Empty or Full charge.",
        "Hold Wario Waft charge at Empty, Half, or Full.",
        "Hold Donkey Kong Giant Punch at Empty or Full charge.",
        "Hold Ivysaur Synthesis charge at Empty, Half, or Full.",
        "Grant R.O.B. unlimited aerial Robo Burner boosts.",
        "Hold R.O.B. Gyro at Empty or Full spin charge.",
        "Lock R.O.B. Robo Beam laser charge to Uncharged, Normal, Super.",
        "Lock Lucas Offense Up buff to Off or Charged.",
        "Hold Squirtle Water Gun at Empty or Full charge.",
        "Lock Sheik stored needle count to 0 or 6.",
        "Force King Dedede side special to Waddle Dee, Doo, or Gordo.",
        "Keep Snake Tranquilizer ammo stocked with unlimited shots.",
        "Allow Snake unlimited Cypher uses without landing resets.",
        "Grant Ganondorf unlimited aerial float duration.",
        "Force Ice Climbers Nana grab throws in a chosen direction.",
        "Force Olimar pluck to pull Red, Yellow, Blue, White, or Purple.",
    ],
    "kControlsDesc": [
        "Choose the button that steps one frame during Frame Advance.",
        "Frames to hold Advance or Rewind before repeating continuously.",
        "Step back a frame (needs Frame Advance + Recording Playback).",
        "Keep D-Pad Up as Taunt or disable it during Training Lab.",
        "Leave D-Pad Down normal or bind it to toggle Frame Advance.",
        "Bind D-Pad Left to load the quick savestate or disable it.",
        "Hold D-Pad Right (0.2s) to save a quick savestate, or Off.",
        "Open Training Lab directly with Start or with Z in Pause.",
        "Allow L+R+D-Pad Down to open the P+ Code Menu in Training.",
        "Switch live between Category Rail and Top Tabs layout.",
    ],
    "kDebugDesc": [
        "Inspect live PSA AnimCmd threads, wait timers, and speed (FSM).",
        "Show frame counters, scene info, and advantage debug text.",
    ],
    "kComboTrainerDesc": [
        "Enable automatic combo reset tracking and recovery looping.",
        "Temporarily disable stage clipping on reset for clean warps.",
        "Return the CPU to its anchor point when hitstun ends.",
        "Move the CPU to a random safe stage spot on each reset.",
        "Constrain random combo resets to grounded floor and platforms.",
        "Assign a random 0-99% CPU damage value on every reset.",
        "Use CPU Damage or a custom percentage when resets warp the CPU.",
        "Set the custom damage applied to the CPU on every reset.",
        "Frames after hitstun (+ waits later, - resets earlier).",
        "Loop offstage CPU spawns with PPlusAI for edgeguard drills.",
        "Choose which 3D floating state meters render above fighters.",
    ],
    "kComboMetersDesc": [
        "Show remaining hitlag freeze frames above the fighter.",
        "Show remaining hitstun frames as a 3D meter during combos.",
        "Show remaining shieldstun frames above a shielding fighter.",
        "Render the 3D directional influence vector ring during hitlag.",
        "Highlight remaining frames until interruptible action opens.",
        "Show remaining ledge invulnerability frames above the fighter.",
        "Show remaining dodge, respawn, or move intangibility frames.",
    ],
    "kTechDesc": [
        "Choose how the CPU techs knockdowns (or Random weights).",
        "Choose the CPU wakeup action after a missed tech.",
        "Hide fighter models briefly during tech roll animations.",
        "Frames after the distinguishable tech frame before hiding model.",
        "Play a sound cue when the CPU commits to a tech option.",
        "Simulate a bad mid-air press to test early/late tech lockouts.",
        "Choose early or late timing inside the valid CPU tech window.",
        "Weight for neutral tech in place when Tech is set to Random.",
        "Weight for tech roll away when Tech is set to Random.",
        "Weight for tech roll toward when Tech is set to Random.",
        "Weight for missed tech knockdown when Tech is set to Random.",
        "Chance for the CPU to stay lying down before waking up.",
        "Weight for neutral getup when Getup is set to Random.",
        "Weight for roll away wakeup when Getup is set to Random.",
        "Weight for roll toward wakeup when Getup is set to Random.",
        "Weight for getup attack when Getup is set to Random.",
    ],
    "kAdvCounterDesc": [
        "Select which hit number in a combo (1-10) to customize.",
        "Use Default counter, turn Off, or set Custom actions here.",
        "Custom grounded counter action for the selected hit index.",
        "Custom airborne counter action for the selected hit index.",
        "Custom out-of-shield counter action for the selected hit.",
        "Actionable frame delay before the custom grounded counter.",
        "Actionable frame delay before the custom airborne counter.",
        "Actionable frame delay before the custom shield counter.",
    ],
    "kSlotsDesc": [
        "Choose whether to manage Player 1 or CPU recording slots.",
        "Select which recording slot (1-6) to inspect or modify.",
        "Open the frame-by-frame stick and button input editor.",
        "Press A twice to erase all recorded frames from this slot.",
        "Select the destination slot number for copying inputs.",
        "Duplicate the selected slot's inputs into the destination.",
    ],
    "kAlterDesc": [
        "Select which recorded frame of the slot to edit.",
        "Set main control stick horizontal value (-80 to 80).",
        "Set main control stick vertical value (-80 to 80).",
        "Set C-Stick horizontal value (-80 to 80) for this frame.",
        "Set C-Stick vertical value (-80 to 80) for this frame.",
        "Set left analog trigger depth (0 to 150) for this frame.",
        "Set right analog trigger depth (0 to 150) for this frame.",
        "Toggle the Attack button input on this recorded frame.",
        "Toggle the Special button input on this recorded frame.",
        "Toggle the Jump button input on this recorded frame.",
        "Toggle the Shield button input on this recorded frame.",
        "Toggle the Smash flag on this recorded frame.",
        "Toggle the C-Stick button flag on this recorded frame.",
        "Toggle Up Taunt input on this recorded frame.",
        "Toggle Side Taunt input on this recorded frame.",
        "Toggle Down Taunt input on this recorded frame.",
    ],
    "kSlotChancesDesc": [
        "Selection weight for Slot 1 during random slot playback.",
        "Selection weight for Slot 2 during random slot playback.",
        "Selection weight for Slot 3 during random slot playback.",
        "Selection weight for Slot 4 during random slot playback.",
        "Selection weight for Slot 5 during random slot playback.",
        "Selection weight for Slot 6 during random slot playback.",
        "Add up to this much random damage on each restore or load.",
    ],
    "kOverlaysDesc": [
        "Preset or Custom colors (lower rows override upper rows).",
        "Overlay color or cue while the fighter is free to act.",
        "Overlay color or cue while the fighter is in hitstun.",
        "Overlay color or cue during invincibility or intangibility.",
        "Overlay color or cue when actionable while hanging on ledge.",
        "Overlay color or cue after missing an aerial L-cancel.",
        "Overlay color or cue once aerial fastfall becomes available.",
        "Overlay color or cue during an aerial autocancel window.",
        "Overlay color or cue while the fighter is crouching.",
        "Overlay color or cue while standing idle in Wait.",
        "Overlay color or cue while walking on the ground.",
        "Overlay color or cue during the initial dash animation.",
        "Overlay color or cue while running across the stage.",
        "Overlay color or cue during a full-hop jump arc.",
        "Overlay color or cue during a short-hop jump arc.",
        "Overlay color or cue once a move reaches its IASA frames.",
        "Overlay color or cue while locked in shield stun.",
    ],
    "kInfoDesc": [
        "Fill all 8 info rows from a quick telemetry preset.",
        "Choose Small, Medium, or Large text for this info panel.",
        "Pick the live frame or physics readout shown in Row 1.",
        "Pick the live frame or physics readout shown in Row 2.",
        "Pick the live frame or physics readout shown in Row 3.",
        "Pick the live frame or physics readout shown in Row 4.",
        "Pick the live frame or physics readout shown in Row 5.",
        "Pick the live frame or physics readout shown in Row 6.",
        "Pick the live frame or physics readout shown in Row 7.",
        "Pick the live frame or physics readout shown in Row 8.",
    ],
    "kLedgedashDesc": [
        "Show the frame-by-frame ledgedash timing bar after ledge drops.",
        "Set how many frame cells the ledgedash timeline displays.",
        "Color grounded actionable invulnerable frames on the bar.",
        "Display the numeric GALINT, ledge frames, and angle strip.",
    ],
    "kFighterSwapDesc": [
        "Select a new character to load into Player 1 mid-match.",
        "Select the costume color index for the swapped Player 1.",
        "Select a new character to load into the CPU mid-match.",
        "Select the costume color index for the swapped CPU.",
        "Reload the chosen fighters in place without leaving the match.",
    ],
    "kThemeDesc": [
        "Choose the background color preset for OSD notification cards.",
        "Adjust OSD card opacity from transparent (0%) to solid (100%).",
        "Choose the chrome accent palette for the in-match Training Lab.",
        "Choose the background color preset for Info Display panels.",
        "Adjust Info Display opacity with drop shadow at low values.",
    ],
    "kReversalDesc": [
        "Enable weighted multi-slot reversals across all four triggers.",
        "Choose On-Hit, On-Shield, On-Getup, or On-Ledge slots to edit.",
        "Primary Slot 1 reversal configured in CPU Counters / Tech / Ledge.",
        DESC_REV_ACTION,
        DESC_REV_DELAY,
        DESC_REV_CHANCE,
        DESC_REV_ACTION,
        DESC_REV_DELAY,
        DESC_REV_CHANCE,
        DESC_REV_ACTION,
        DESC_REV_DELAY,
        DESC_REV_CHANCE,
        DESC_REV_ACTION,
        DESC_REV_DELAY,
        DESC_REV_CHANCE,
    ],
    "kCpuDiDesc": [
        "Set how the CPU angles survival or combo trajectory DI.",
        "Set how many smash DI shifts the CPU inputs during hitlag.",
        "Choose the direction for CPU hitlag smash DI inputs.",
        "Alternate stick angles each hitlag frame for faster CPU SDI.",
        "Set the stick direction held at hitlag end for automatic SDI.",
    ],
    "kCpuCountersDesc": [
        "Choose the CPU reaction after grounded hitstun or knockdown.",
        "Choose the CPU reaction once airborne hitstun ends.",
        "Choose the CPU out-of-shield reaction after blocking a hit.",
        "Actionable frames before CPU counters or ledge getups fire.",
        "Customize unique CPU counter actions for individual combo hits.",
        "Configure weighted multi-slot reversals with per-slot delays.",
        "Track P1 habits with live HUD, or also bias CPU counter options.",
        "Automatically L-cancel CPU aerial attacks before landing.",
        "Set aerial and fastfall timing for CPU SHFFL and FFL actions.",
    ],
    "kCpuDefenseDesc": [
        DESC_INFINITE_SHIELD,
        "Set the target shield health held by CPU Infinite Shield (60 max).",
        "Angle the CPU's shield bubble while guarding.",
        "Prevent hits from launching the CPU (locks CPU damage to 0%).",
        "Set how fast the CPU mashes inputs to break out of grabs.",
        "Choose whether grab breaks leave the CPU grounded or airborne.",
        "Set CPU mash rate to escape bury, freeze, sleep, and status traps.",
    ],
    "kRailDesc": [
        "Frame advance, Player 1 damage, speed, camera, and controls.",
        "One-click P+ practice presets and Combo Trainer settings.",
        "CPU damage, DI, SDI, tech habits, counters, and recovery AI.",
        "Record, edit, loop, mirror, and export multi-slot inputs.",
        "Hitboxes, heatmaps, DI vectors, overlays, and info panels.",
        "Toggle on-screen frame data and technique notifications.",
        "Fighter swap, active stage options, and character mechanics.",
        "Press A twice to reset all Training Lab settings to defaults.",
        DESC_TRAINING_MENU,
        DESC_EXIT_CSS,
    ],
}

# Indexed by ui::pages::PageId, plus P_COUNT for Category Rail entries.
PAGE_TABLES = [
    ("kGeneralDesc", "P_GENERAL"),
    ("kScenariosDesc", "P_SCENARIOS"),
    ("kCpuDesc", "P_CPU"),
    ("kRecordingDesc", "P_RECORDING"),
    ("kDisplayDesc", "P_DISPLAY"),
    ("kOsdDesc", "P_OSD_MENU"),
    ("kStageCastDesc", "P_STAGE_CAST"),
    ("kControlsDesc", "P_CONTROLS"),
    ("kDebugDesc", "P_DEBUG"),
    ("kComboTrainerDesc", "P_COMBO_TRAINER"),
    ("kComboMetersDesc", "P_COMBO_METERS"),
    ("kTechDesc", "P_TECH"),
    ("kAdvCounterDesc", "P_ADVANCED_COUNTER"),
    ("kSlotsDesc", "P_SLOTS"),
    ("kAlterDesc", "P_ALTER_INPUTS"),
    ("kSlotChancesDesc", "P_HMN_CHANCES"),
    ("kSlotChancesDesc", "P_CPU_CHANCES"),
    ("kOverlaysDesc", "P_HMN_OVERLAYS"),
    ("kOverlaysDesc", "P_CPU_OVERLAYS"),
    ("kInfoDesc", "P_HMN_INFO"),
    ("kInfoDesc", "P_CPU_INFO"),
    ("kLedgedashDesc", "P_LEDGEDASH_HUD"),
    ("kFighterSwapDesc", "P_FIGHTER_SWAP"),
    ("kThemeDesc", "P_THEME"),
    ("kReversalDesc", "P_REVERSALS"),
    ("kCpuDiDesc", "P_CPU_DI"),
    ("kCpuCountersDesc", "P_CPU_COUNTERS"),
    ("kCpuDefenseDesc", "P_CPU_DEFENSE"),
    ("kRailDesc", "P_COUNT (Category Rail entries)"),
]

def build_bpe(strings, max_pairs=128):
    seqs = [list(s.encode("ascii")) for s in strings]
    for s, seq in zip(strings, seqs):
        for b in seq:
            if b < 0x20 or b >= 0x7F:
                raise ValueError(f"Non-printable ASCII byte {b:#x} in {s!r}")
    pairs = []
    for tok in range(0x80, 0x80 + max_pairs):
        counts = {}
        for seq in seqs:
            for i in range(len(seq) - 1):
                p = (seq[i], seq[i + 1])
                counts[p] = counts.get(p, 0) + 1
        if not counts:
            break
        best_p, best_c = max(counts.items(), key=lambda kv: (kv[1], -kv[0][0], -kv[0][1]))
        if best_c <= 2:
            break
        pairs.append(best_p)
        a, b = best_p
        next_seqs = []
        for seq in seqs:
            out = []
            i = 0
            n = len(seq)
            while i < n:
                if i + 1 < n and seq[i] == a and seq[i + 1] == b:
                    out.append(tok)
                    i += 2
                else:
                    out.append(seq[i])
                    i += 1
            next_seqs.append(out)
        seqs = next_seqs
    return pairs, seqs


def decode_one(stream, offset, pairs):
    out = []
    max_sp = 0
    i = offset
    while stream[i] != 0:
        stack = [stream[i]]
        i += 1
        while stack:
            if len(stack) > max_sp:
                max_sp = len(stack)
            c = stack.pop()
            if c < 0x80:
                out.append(chr(c))
            else:
                a, b = pairs[c - 0x80]
                stack.append(b)
                stack.append(a)
    return "".join(out), max_sp


def format_u8_array(values, indent="        ", per_line=16):
    rows = []
    for i in range(0, len(values), per_line):
        chunk = values[i:i + per_line]
        rows.append(indent + ", ".join(f"0x{v:02X}" for v in chunk) + ",")
    if rows:
        rows[-1] = rows[-1].rstrip(",")
    return "\n".join(rows)


def format_u16_array(values, indent="        ", per_line=12):
    rows = []
    for i in range(0, len(values), per_line):
        chunk = values[i:i + per_line]
        rows.append(indent + ", ".join(str(v) for v in chunk) + ",")
    if rows:
        rows[-1] = rows[-1].rstrip(",")
    return "\n".join(rows)


def generate_cpp():
    uniq_tables = []
    for t, _ in PAGE_TABLES:
        if t not in uniq_tables:
            uniq_tables.append(t)

    uniq_strings = []
    str_to_id = {}
    for t in uniq_tables:
        for s in TABLES[t]:
            if s not in str_to_id:
                str_to_id[s] = len(uniq_strings)
                uniq_strings.append(s)

    pairs, seqs = build_bpe(uniq_strings, 128)

    stream = []
    str_offsets = []
    for seq in seqs:
        str_offsets.append(len(stream))
        stream.extend(seq)
        stream.append(0)
    if len(stream) > 65535:
        raise ValueError(f"Compressed stream {len(stream)} exceeds u16 offset range")

    max_len = max(len(s) for s in uniq_strings)
    if max_len >= 127:
        raise ValueError(f"Description length {max_len} exceeds 127-byte decode buffer")

    max_stack = 0
    for s, off in zip(uniq_strings, str_offsets):
        decoded, sp = decode_one(stream, off, pairs)
        if decoded != s:
            raise AssertionError(f"BPE round-trip mismatch: {decoded!r} != {s!r}")
        if sp > max_stack:
            max_stack = sp
    if max_stack > 32:
        raise ValueError(f"BPE stack depth {max_stack} exceeds 32")

    row_offsets = []
    table_span = {}
    for t in uniq_tables:
        start = len(row_offsets)
        for s in TABLES[t]:
            row_offsets.append(str_offsets[str_to_id[s]])
        table_span[t] = (start, len(TABLES[t]))

    pair_bytes = []
    for a, b in pairs:
        pair_bytes.append(a)
        pair_bytes.append(b)

    page_rows = []
    for idx, (t, cmt) in enumerate(PAGE_TABLES):
        start, count = table_span[t]
        comma = "," if idx + 1 < len(PAGE_TABLES) else " "
        page_rows.append(f"        {{{start:3d}, {count:2d}}}{comma} // {cmt}")

    cpp = f"""// Generated by scripts/gen_menu_descriptions.py - edit descriptions there.
#include "extra/menu_descriptions.h"

#include "main/ui/pages/pages.h"

namespace tm {{
namespace extra {{
    using namespace ui::pages;

    struct DescSpan {{
        u16 start;
        u8 count;
    }};

    enum {{
        kBpeFirst = 0x80,
        kBpePairCount = {len(pairs)},
        kDescBufSize = 128,
        kBpeStackSize = 32
    }};

    // 128-pair BPE dictionary: token (0x80 + i) expands to pair (2*i, 2*i + 1).
    static const u8 kBpePairs[kBpePairCount * 2] = {{
{format_u8_array(pair_bytes)}
    }};

    // Packed NUL-terminated BPE-encoded unique description strings ({len(stream)} B vs {sum(len(s) + 1 for s in uniq_strings)} B raw).
    static const u8 kDescStream[{len(stream)}] = {{
{format_u8_array(stream)}
    }};

    // Byte offset into kDescStream for every row of each unique description table.
    static const u16 kRowOffsets[{len(row_offsets)}] = {{
{format_u16_array(row_offsets)}
    }};

    // Indexed by ui::pages::PageId, plus P_COUNT for Category Rail entries.
    static const DescSpan kTables[P_COUNT + 1] = {{
{chr(10).join(page_rows)}
    }};

    static char s_descBuf[kDescBufSize];

    const char* optionDesc(u8 pageId, u8 row)
    {{
        if (pageId > P_COUNT)
        {{
            return NULL;
        }}
        const DescSpan& t = kTables[pageId];
        if (row >= t.count)
        {{
            return NULL;
        }}
        const u8* p = kDescStream + kRowOffsets[t.start + row];
        u8 stack[kBpeStackSize];
        u32 out = 0;
        while (*p != 0 && out + 1 < kDescBufSize)
        {{
            u32 sp = 0;
            stack[sp++] = *p++;
            while (sp > 0 && out + 1 < kDescBufSize)
            {{
                u8 c = stack[--sp];
                if (c < kBpeFirst)
                {{
                    s_descBuf[out++] = static_cast<char>(c);
                }}
                else
                {{
                    u32 idx = static_cast<u32>(c - kBpeFirst);
                    if (idx < kBpePairCount && sp + 2 <= kBpeStackSize)
                    {{
                        stack[sp++] = kBpePairs[idx * 2 + 1];
                        stack[sp++] = kBpePairs[idx * 2];
                    }}
                }}
            }}
        }}
        s_descBuf[out] = '\\0';
        return s_descBuf;
    }}
}}
}}
"""
    return cpp


def main():
    root = pathlib.Path(__file__).resolve().parent.parent
    out_path = root / "src" / "extra" / "menu_descriptions.cpp"
    cpp = generate_cpp()
    if "--check" in sys.argv[1:]:
        current = out_path.read_bytes().decode("ascii")
        if current != cpp:
            sys.stderr.write("error: src/extra/menu_descriptions.cpp is out of date; run scripts/gen_menu_descriptions.py\n")
            return 1
        print("menu_descriptions: up to date")
        return 0
    out_path.write_bytes(cpp.encode("ascii"))
    print(f"wrote {out_path} ({len(cpp)} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
