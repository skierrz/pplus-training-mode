"""Emulator test of CPU control (run through emu.py; settings come from the
environment the same way).

  test-cpu REL [--only NAME ...] [--shard I/N] [--list]
      One boot, Mario (P1) against a Fox CPU on Final Destination, one CPU
      thread. Every scenario starts from a Dolphin savestate taken once the
      match runs, and is stepped one game frame at a time while the fighters
      are read from game memory (status, position, hitlag, hitstun, shield
      health, the input objects, aiMgr's per-slot AI flag), never from the
      plugin's own claims.

      Driver scenarios (the harness script source, src/main/cpu/script_source.*):
        c1 hold shield, c2 jump every 40 frames, c3 frame-exact sequence,
        c4 stick movement, c5 silencing the AI off-stage (and three
        baselines without it), e driving the human P1, di (trajectory and
        smash DI with the AI flag set and cleared), tech (the tech window,
        rolls and the lockout).
      CPU Options scenarios (the settings block written through the harness):
        shield (Behavior Shield, shield hit, Infinite Shields), counter-ground
        (Up Smash, delay 0 and 5, against the first actionable frame measured
        by a probe), counter-shield (Grab), counter-air (Airdodge),
        tdi (Inwards / None / Outwards), sdi (amount 0/1/3), techopts (each
        Tech Option and Get Up Option), techrandom (30 trials), silence
        (off-stage under the options, then with them off), recovery
        (Recovery Game AI: walked off the stage, the game's AI gets the
        CPU), recovery-hit (the same after Mario's forward smash sent it off
        the stage in tumble, without and with an air counter action;
        recovery-hit-mario plays it in a match against a Mario CPU),
        intangible,
        menu (the CPU rows in the lab menu, chance rebalancing), training
        (CPU Control Training leaves Training's option alone; its Control
        option wins over Lab), multi (3 CPUs), nana (Ice Climbers CPU),
        finish (a match ends with control active and the next one starts
        clean).
      Counter actions and the newer options:
        actions-ground / actions-air / actions-shield (every action of each
        Counter Action list: the status it starts and the frame against the
        first actionable frame a probe measured; the wavedashes and aerials
        on the first airborne frame; Random against the counter pools),
        shield-angle (the guard status's stick floats), powershield
        (Behavior Perfect Shield against a dash attack: guard on the hit's
        frame or the one before, no shield damage), asdi (the shift as
        hitlag ends), tdi-measured (Inwards / Outwards perpendicular to the
        knockback read from memory), tdi-natural, custom-tdi (the editor
        driven with port 1's pad over Training's pause, then hits using its
        inputs), custom-tdi-start (the editor in the menu Start opens),
        move-freeze
        (Move CPU, Freeze CPU, Controlled By), grab (mash reductions and
        hold times per Grab Escape, Grab Release), advanced (Advanced
        Counter Options set through the lab menu), tech-wait (Miss Tech
        Wait Chance), tech-stage (a match on Bowser's Castle:
        teching on stairs and floor below the platform the CPU stood on),
        place-platforms (a match on Battlefield: Place CPU puts the CPU
        exactly at the height of each pass-through platform and of the main
        floor, and it must stay standing there).
      Trajectory DI against the launch, and cues (src/main/cpu/cue.h):
        tdi-angles (each Trajectory DI choice on three hits: the change of
        the launch angle in memory against the DI rule), cue (five ground
        and five air actions cued on a standing and an airborne CPU against
        a script pressing them on the same frame, a cue for a later frame,
        cancelling, a cue in hitstun and one while a counter action runs,
        holds), landing (four landings without an attack, which of them
        land in LandingLight; a Jab cued in the air on the first frame a
        scripted A comes out after the landing).
      The game's rules read from its code, each against a scripted human:
        shield-end (four out-of-shield actions and a Jab cued with shield
        let go, at the end of shield stun), wiggle (pushes in tumble around
        the end of hitstun), air-jump (a jump after hitstun only while not
        rising), and tech-trap / tech-lockout above under the 41-frame rule.
      The recovery routines (Recovery Options > Recovery: Lab, src/extra/
      recovery/), the CPU knocked off the stage by Mario's forward smash
      from four setups, the routine's random numbers fixed by a seed
      (the extra module's recovery test block):
        recover (each option alone and all on: the game shows only enabled
        options, the up special is aimed by its rule, every option is used,
        the CPU comes back, the AI stays off), recover-counter (the air
        counter action first, then the routine), recover-hit (a hit during
        the recovery: the lab's DI, then the recovery again), recover-menu
        (the character's page), recover-filter (Recovery Game AI: the hook
        sees the AI's controls; the AI's air dodge below the ledge is taken
        out of the frame). recover-falco, recover-counter-falco,
        recover-hit-falco and recover-menu-falco play them in a match
        against a Falco CPU, recover-filter-mario against a Mario CPU.
      P+ character techniques (the end of the shield and ground lists):
        oos-techniques (on the main match's CPU) and oos-techniques-wolf,
        -ness, -lucas, -peach, -mewtwo, -ganondorf, -yoshi in their own matches: Down B
        out of shield (the shine or magnet on the first airborne frame), the
        double jump cancelled and float aerials where the character has
        them, and as None where it has not; the menu greys them for a CPU
        without them. oos-probe (only with --only) prints the
        measurements they rest on.
      --cpu NAME plays the main match against another CPU character.
      recover-measure and recover-ledge (only with --only, with --cpu fox or
      falco) print the measurements the routines' numbers come from.

Every scenario runs alone with --only NAME: the savestates are taken first
in every run, and each scenario sets up the options, the script block and
the CPU count it needs (nana plays its own match against Ice Climbers,
tech-stage, place-platforms, the -falco and the -mario scenarios their own
too).

The settings layout is main::CpuSettings (src/main/settings.h) with
cpu::CpuOptions (src/main/cpu/cpu_settings.h) at its offset 3.
"""
import argparse
import math
import os
import re
import struct
import time

import emu as E
import lab_tests as LT
import loader_tests as L
import nav
import plugin_input as PI
from harness import HarnessError, Timeout

A, B, JUMP, GUARD = 0x1, 0x2, 0x4, 0x8
P1, CPU = 0, 1  # players (0-based)

# ftEntry (0x244 bytes each, array at ftEntryManager+0): +0x4 entry id, +0xA
# active instance, +0x18 slot, +0x28 ftOwner*, +0x2C ftInput* (+0xC main
# input, +0x10 Nana's), +0x34 instances {kind, Fighter*} 8 bytes apart,
# +0x58 player number.
ENTRY_MANAGER, ENTRY_SIZE = LT.ENTRY_MANAGER, LT.ENTRY_SIZE
NANA_INSTANCE = 1
# g_aiMgr -> aiMgr; +0x84 + slot: nonzero while the AI drives that slot.
AI_MGR, AI_FLAGS = 0x80B8A2D8, 0x84
# Fighter +0x60 -> module accesser, +0xD8 -> module enumeration: +0xC posture
# (+0xC x, +0x10 y, +0x40 facing), +0x14 situation (+0x2C kind, 2 = air),
# +0x44 stop (+0x10 hitlag left), +0x64 work (+0x20 instance work: ints at
# +0xC, floats at +0x14; int 0x38 hitstun, float 3 shield health), +0x70
# status (+0x34 kind).
AIR = 2

STATUS = {
    "Wait": 0x00, "Walk": 0x01, "Dash": 0x03, "Run": 0x04, "Turn": 0x06, "Turn_Dash": 0x07, "Turn_Run": 0x08,
    "Jump_Squat": 0x0A, "Jump": 0x0B, "Jump_Aerial": 0x0C, "Fall": 0x0E, "Fall_Aerial": 0x0F,
    "Fall_Special": 0x10, "Squat": 0x11, "Squat_Wait": 0x12, "Squat_Rv": 0x15, "Landing": 0x16, "Landing_Light": 0x17,
    "Landing_Attack_Air": 0x18, "Landing_Fall_Special": 0x19,
    "Guard_On": 0x1A, "Guard": 0x1B, "Guard_Off": 0x1C, "Guard_Damage": 0x1D,
    "Escape": 0x1E, "Escape_F": 0x1F, "Escape_B": 0x20, "Escape_Air": 0x21,
    "Attack": 0x24, "Attack_Dash": 0x26, "Attack_S3": 0x27, "Attack_Hi3": 0x28, "Attack_Lw3": 0x29,
    "Attack_S4_Start": 0x2A, "Attack_Lw4_Start": 0x2D, "Attack_Hi4_Start": 0x30,
    "Attack_Air": 0x33, "Catch": 0x34, "Catch_Pull": 0x35, "Catch_Wait": 0x39,
    "Damage": 0x43, "Damage_Air": 0x44, "Damage_Fly": 0x45, "Damage_Fly_Roll": 0x46,
    "Damage_Fall": 0x49, "Down": 0x4A, "Down_Wait": 0x4D, "Down_Stand": 0x51, "Down_Stand_Fb": 0x52,
    "Down_Stand_Attack": 0x53, "Passive": 0x60, "Passive_Fb": 0x61,
    "Cliff_Catch": 0x74, "Cliff_Wait": 0x75, "Air_Lasso": 0x7F,
    "Slip": 0x89, "Slip_Down": 0x8A, "Slip_Wait": 0x8D, "Slip_Stand": 0x8E,
    "Slip_Stand_Attack": 0x8F, "Slip_Stand_F": 0x90, "Slip_Stand_B": 0x91,
    "Dead": 0xBD, "Rebirth": 0xBE, "Standby": 0x10B,
}
NAME = {v: k for k, v in STATUS.items()}
HIT = {STATUS[n] for n in ("Damage", "Damage_Air", "Damage_Fly", "Damage_Fly_Roll")}
# Until a tumbling fighter lands (its hitstun can end in the air first, in
# Damage_Fall: Ice Climbers after a down smash at 50%).
FLYING = HIT | {STATUS["Damage_Fall"]}
GONE = {STATUS["Dead"], STATUS["Standby"], STATUS["Rebirth"]}

# main::Settings: the CPU block (cpu::CpuSettings) starts at +68.
SET_CPU = LT.SET_CPU_PERCENT + 3  # main::CpuSettings::options
SET_CPU_PERCENT, SET_CPU_LOCK = LT.SET_CPU_PERCENT, LT.SET_LOCK_CPU
# main::Settings::cpuTables: the Per-Hit Counters (Advanced Counter) pages
# (src/main/settings.h:275, cpu::CpuTables in src/main/cpu/cpu_settings.h:100).
SET_CPU_TABLES, CPU_TABLES_SIZE = 296, 76
CPU_FIELDS = {"control": 0, "behavior": 1, "tdi": 2, "sdiAmount": 3, "sdiDir": 4, "counterGround": 5,
              "counterAir": 6, "counterShield": 7, "counterDelay": 8, "infiniteShields": 9,
              "shieldHealth": 10, "intangible": 11, "tech": 12, "getup": 13, "techChance": 14,
              "getupChance": 18, "asdi": 22, "shieldAngle": 23, "grabEscape": 24, "grabRelease": 25,
              "controlledBy": 26, "techSound": 27, "techTrap": 28, "techLockout": 29, "missTechWait": 30,
              "advHit": 31, "advLogic": 32, "advGround": 33, "advAir": 34, "advShield": 35,
              "advDelayGround": 36, "advDelayAir": 37, "advDelayShield": 38, "recovery": 39,
              "ledgeOption": 40, "clatterEscape": 41, "adaptiveMode": 42, "dualStickSdi": 43,
              "autoLCancel": 44, "shfflTiming": 45}
SHFFL_TIMING = {"Frame-Perfect": 0, "Early": 1, "Normal": 2, "Late": 3, "Random": 4}
RECOVERY = {"Game": 0, "PPlusAI": 1, "Off": 2}
BEHAVIOR = {"Stand": 0, "Shield": 1, "Crouch": 2, "Jump": 3, "Perfect Shield": 4}
# Indices of the rows' labels (src/main/cpu/cpu_settings.h, ui/pages/cpu_options.cpp).
TDI = {"Random": 0, "Inwards": 1, "Outwards": 2, "Survival": 3, "None": 4}
ASDI = {"Auto": 0, "Away": 1, "Towards": 2, "Left": 3, "Right": 4, "Up": 5, "Down": 6}
ANGLE = {"Neutral": 0, "Up": 1, "Towards": 2, "Down": 3, "Away": 4}
MASH = {"None": 0, "Medium": 1, "High": 2, "Perfect": 3}
SDI = {"Auto": 0, "Random": 1, "Away": 2, "Towards": 3, "Left": 4, "Right": 5, "Up": 6, "Down": 7}
GROUND = {"None": 0, "Spotdodge": 1, "Shield": 2, "Grab": 3, "Up Smash": 8, "Jab": 13,
          "Short Hop": 18, "Full Hop": 19,
          "SHFFL Nair": 51, "SHFFL Fair": 52, "SHFFL Bair": 53, "SHFFL Uair": 54, "SHFFL Dair": 55}
AIR_ACTIONS = {"None": 0, "Airdodge": 1, "Jump Neutral": 4, "Tumble Fastfall": 16, "Wiggle Fastfall": 17,
               "FFL Nair": 29, "FFL Fair": 30, "FFL Bair": 31, "FFL Uair": 32, "FFL Dair": 33}
SHIELD_ACTIONS = {"None": 0, "Grab": 1,
                  "SHFFL Nair": 44, "SHFFL Fair": 45, "SHFFL Bair": 46, "SHFFL Uair": 47, "SHFFL Dair": 48}
LAB, TRAINING = 1, 0  # CPU Control
# CPU Options rows (main's tree plus CPU Control) and Tech Options rows.
CPU_CONTROL_ROW, CPU_TECH_ROW, CPU_BEHAVIOR_ROW = 2, 3, 8
TECH_INVIS_ROW, TECH_DELAY_ROW, TECH_SOUND_ROW, TECH_IN_PLACE_ROW = 2, 3, 4, 7
SET_TECH_INVIS, SET_TECH_DELAY = 288, 289
# P+'s first distinguishable tech frame per character (src/main/game/tech_frames.cpp).
TECH_DIST = {"fox": 4, "mario": 8, "zelda": 3, "sheik": 4}
FT_ZELDA, FT_SHEIK = 0x0D, 0x0E
GETUP_SUBS = set(range(0xAF, 0xB3)) | set(range(0xB8, 0xBC)) | set(range(0xE6, 0xEA))
TECH = {"Random": 0, "In Place": 1, "Away": 2, "Towards": 3, "None": 4}
GETUP = {"Random": 0, "Stand": 1, "Away": 2, "Towards": 3, "Attack": 4}
SHIELDS = {"Off": 0, "Until Hit": 1, "On": 2}

# Training's CPU option (ifMinigameTraining+0x24): 1 Attack, 2 Control, 3 Stop.
TRAINING_OPTION = {"Attack": 1, "Control": 2, "Stop": 3, "Shield": 6}
# P1's smash attacks, as main-stick positions held with A.
FSMASH, DSMASH, JAB, USMASH = (255, 128), (128, 0), (128, 128), (128, 255)
DTILT = (128, 60)
STICKS = {"neutral": (0.0, 0.0), "left": (-1.0, 0.0), "right": (1.0, 0.0), "up": (0.0, 1.0),
          "down": (0.0, -1.0)}
NEUTRAL_STEP = (0, 0.0, 0.0, 1)
CROUCH_STEP = (0, 0.0, -1.0, 1)


def u32(b, o):
    return struct.unpack_from(">I", b, o)[0]


def s32(b, o):
    return struct.unpack_from(">i", b, o)[0]


def f32(b, o):
    return struct.unpack_from(">f", b, o)[0]


class CpuError(RuntimeError):
    pass


# ---- game memory ------------------------------------------------------------------

class Game:
    """Fighters, input objects and AI flags, read from memory."""

    def __init__(self, emu):
        self.emu = emu
        self.mods = {}

    def entries(self):
        e = self.emu
        arr = e.ptr(ENTRY_MANAGER)
        out = []
        for i in range(min(e.u32(ENTRY_MANAGER + 4), 9) if arr else 0):
            addr = arr + i * ENTRY_SIZE
            raw = e.read(addr, 0x60)
            if s32(raw, 4) < 0 or raw[0xA] > 3 or not e.is_ram(u32(raw, 0x2C)):
                continue
            out.append({"addr": addr, "entry": s32(raw, 4), "slot": s32(raw, 0x18), "player": s32(raw, 0x58),
                        "owner": u32(raw, 0x28), "input": u32(raw, 0x2C), "active": raw[0xA],
                        "kinds": [s32(raw, 0x30 + 8 * k) for k in range(2)],
                        "fighters": [u32(raw, 0x34 + 8 * k) for k in range(2)]})
        return out

    def entry(self, player):
        for en in self.entries():
            if en["player"] == player:
                return en
        raise CpuError(f"no fighter for player {player + 1}")

    def inputs(self, en):
        """(main input, Nana's input or None)."""
        raw = self.emu.read(en["input"] + 0xC, 8)
        sub = u32(raw, 4)
        return u32(raw, 0), (sub if self.emu.is_ram(sub) else None)

    def pad(self, ai):
        raw = self.emu.read(ai, 0x30)
        return {"buttons": u32(raw, 0xC), "x": f32(raw, 0x10), "y": f32(raw, 0x14), "cont": s32(raw, 8)}

    def ai_flag(self, slot):
        return self.emu.u8(self.emu.u32(AI_MGR) + AI_FLAGS + slot)

    def modules(self, fighter):
        m = self.mods.get(fighter)
        if m is None:
            e = self.emu
            acc = e.u32(fighter + 0x60)
            enum = e.read(e.u32(acc + 0xD8), 0xA8)
            wm = u32(enum, 0x64)
            work = e.u32(wm + 0x20)
            wr = e.read(work + 0xC, 0x10)
            m = {"fighter": fighter, "acc": acc, "motion": u32(enum, 0x8), "posture": u32(enum, 0xC),
                 "situation": u32(enum, 0x14), "stop": u32(enum, 0x44), "work": wm,
                 "status": u32(enum, 0x70), "kinetic": u32(enum, 0x7C), "visibility": u32(enum, 0x58),
                 "shadow": u32(enum, 0xA4), "ints": u32(wr, 0), "floats": u32(wr, 8)}
            self.mods[fighter] = m
        return m

    def _fighter_read(self, en, m):
        e = self.emu
        p = e.read(m["posture"] + 0xC, 0x38)
        return {"x": f32(p, 0), "y": f32(p, 4), "lr": f32(p, 0x34),
                "status": e.u32(m["status"] + 0x34),
                "hitlag": s32(e.read(m["stop"] + 0x10, 4), 0),
                "air": e.u32(m["situation"] + 0x2C) == AIR,
                "hitstun": s32(e.read(m["ints"] + 0x38 * 4, 4), 0),
                "shield": e.f32(m["floats"] + 12),
                "damage": e.f32(e.u32(en["owner"]) + 0x24)}

    def fighter(self, en, instance=None):
        ft = en["fighters"][en["active"] if instance is None else instance]
        m = self.modules(ft)
        w = m.get("_fwatch")
        if w is None:
            w = self.emu.watch(self._fighter_read)
            m["_fwatch"] = w
        return w.read(en, m)

    def _extra_read(self, m):
        e = self.emu
        out = {"tilt": None, "mash": None, "kb": None, "psbit": 0, "psdrop": 0, "reflect": False,
               "lcancel": s32(e.read(m["ints"] + 90 * 4, 4), 0)}
        la, sw = struct.unpack(">2I", e.read(m["work"] + 0x20, 8))
        if e.is_ram(la):
            la_bits, la_n = struct.unpack(">2I", e.read(la + 0x1C, 8))
            if la_n > 3 and e.is_ram(la_bits):
                # P+'s powershield bit: LA-Bit[96] (Fighter.pac GuardDamage, PowerShield.asm:140).
                out["psbit"] = e.u32(la_bits + 12) & 1
        if e.is_ram(sw):
            sw_ints, sw_in, sw_fl, _, sw_bits, sw_bn = struct.unpack(">6I", e.read(sw + 0xC, 0x18))
            if sw_in > 3 and e.is_ram(sw_ints):
                # RA-Basic[3]: set to IC-Basic[23079] (5) only on the RA-Basic[1] > 0
                # powershield branch of GuardDamage (both Fighter.pac and FitYoshi.pac).
                out["psdrop"] = e.u32(sw_ints + 12)
            if e.is_ram(sw_fl):
                out["tilt"] = struct.unpack(">2f", e.read(sw_fl + 0x10, 8))
            if sw_bn > 0 and e.is_ram(sw_bits):
                # RA-Bit[10]: set while guarding on the frame a projectile reflects.
                out["reflect"] = bool(e.u32(sw_bits) & 0x400)
        grab = e.u32(m["acc"] + 0x68)
        mash = e.u32(grab + 0x10)
        if e.is_ram(mash):
            out["mash"] = e.f32(mash + 0xC)
        k = e.u32(m["kinetic"] + 0x14)
        if e.is_ram(k):
            kb = e.u32(k + 0x4C)
            if e.is_ram(kb):
                out["kb"] = struct.unpack(">2f", e.read(kb + 8, 8))
        mot = e.read(m["motion"] + 0x40, 0x1C)
        out["motion"] = f32(mot, 0)
        out["sub"] = u32(mot, 0x18)
        out["vis"] = e.u8(m["visibility"] + 0xE)
        sh = e.u8(m["shadow"] + 0x44)
        out["shadow"] = (sh >> 6) & 1       # soShadowModuleImpl activeStatus (bit 6, 0x40)
        out["shadow_proj"] = (sh >> 1) & 1  # soShadowModuleImpl isDispProjection (bit 1, 0x02)
        cur = e.u8(m["fighter"] + 0x138)
        out["cursor"] = (cur >> 2) & 1      # Fighter::setCursor (bit 2, 0x04)
        out["name_cursor"] = (cur >> 1) & 1 # Fighter::setNameCursor (bit 1, 0x02)
        return out

    def extra(self, en):
        """Values the newer options act on, read from the game:
        - tilt: status work floats 4/5 while guarding (the guard status
          stores the shield's stick there as 0x21000004/5, 0x80874BD0/BF0);
        - mash: frames left in a grab ([[Fighter+0x60]+0x68]+0x10, +0xC; P+'s
          Grab.asm reduces the same float);
        - kb: the knockback speed (kinetic module +0x14 -> +0x4C, +0x8/+0xC,
          where P+'s custom AI function reads it);
        - motion: the animation frame (motion module +0x40);
        - sub: the subaction (motion module +0x58, soMotionModuleImpl's
          m_kind)."""
        ft = en["fighters"][en["active"]]
        m = self.modules(ft)
        w = m.get("_xwatch")
        if w is None:
            w = self.emu.watch(self._extra_read)
            m["_xwatch"] = w
        return w.read(m)


def sname(s):
    return NAME.get(s, f"{s:#x}")


# ---- plugin blocks -----------------------------------------------------------------

class Script(PI.Block):
    """The module's first harness script block (scripts/plugin_input.py)."""

    def __init__(self, emu):
        self.emu = emu
        self.addr = None

    def find(self):
        self.addr = PI.PluginInput(self.emu).blocks[0].addr


class CueBlock:
    """The module's cue test block (cpu::CueTestBlock in
    src/main/cpu/options.cpp): an operation of src/main/cpu/cue.h run once
    in the next frame the CPU Options drive the player, and the state of
    the player's cue written back every such frame."""
    MAGIC = 0x544D4355
    LOG = re.compile(r"cpu: cue test block at ([0-9a-f]{8})")
    CUE, CANCEL, HOLD, HOLD_REPEAT, RELEASE = 1, 2, 3, 4, 5
    STATES = ["None", "Waiting", "Running", "Done", "Missed"]

    def __init__(self, emu):
        self.emu = emu
        self.addr = None

    def find(self):
        found = self.LOG.findall(self.emu.log_text())
        if not found:
            raise CpuError("no cue test block line in the log")
        self.addr = int(found[-1], 16)
        if self.emu.u32(self.addr) != self.MAGIC:
            raise CpuError(f"no cue test block at {self.addr:#x}")

    def op(self, op, lst=0, index=0, delay=0, buttons=0, x=0.0, y=0.0, ref=0, player=CPU):
        """delay 0: as soon as the CPU can; n: the nth frame from the one the
        operation runs in."""
        gen = self.emu.u32(self.addr + 4) + 1
        body = struct.pack(">IiIiiiIffI", gen, player, op, lst, index, delay, buttons, x, y, ref)
        self.emu.h.call("write_mem", addr=self.addr + 4, hex=body.hex())

    def status(self):
        raw = self.emu.read(self.addr + 0x2C, 16)
        seen, at, state, start = struct.unpack(">Iiii", raw)
        return {"seen": seen, "at": at, "state": self.STATES[state] if 0 <= state < 5 else state, "start": start}


class Options:
    """The CPU Options block in the lab's settings."""

    def __init__(self, emu):
        self.emu = emu
        self.base = None

    def find(self):
        self.base = LT.Lab(self.emu, []).find_settings()

    def set(self, **values):
        for name, value in values.items():
            if isinstance(value, (list, tuple)):
                data = bytes(value)
            else:
                data = bytes([value])
            self.emu.h.call("write_mem", addr=self.base + SET_CPU + CPU_FIELDS[name], hex=data.hex())

    def get(self, name):
        return self.emu.u8(self.base + SET_CPU + CPU_FIELDS[name])

    def cpu_percent(self, percent, lock):
        self.emu.h.call("write_mem", addr=self.base + SET_CPU_PERCENT,
                        hex=struct.pack(">HB", percent, int(lock)).hex())


class States:
    """Dolphin savestates next to the SD image (outside the repository)."""

    def __init__(self):
        self.dir = os.path.join(os.path.dirname(os.path.abspath(E.env("SD_IMAGE"))), "states")
        os.makedirs(self.dir, exist_ok=True)
        self.saved = set()

    def path(self, name):
        return os.path.join(self.dir, f"cpu-{name}.sav")

    def save(self, emu, name):
        emu.h.call("save_state", path=self.path(name))
        self.saved.add(name)

    def load(self, emu, name):
        """Loaded paused, any pending P1 pad script dropped."""
        emu.h.call("pause")
        emu.h.call("pad_clear", port=0)
        emu.h.call("load_state", path=self.path(name))


class Result:
    def __init__(self, name):
        self.name, self.ok, self.lines, self.shots, self.table = name, True, [], [], []

    def check(self, what, ok):
        self.lines.append(f"{'ok  ' if ok else 'FAIL'} {what}")
        self.ok = self.ok and bool(ok)

    def note(self, text):
        self.lines.append(f"     {text}")

    def row(self, what, expected, observed, ok):
        """One row of the summary table (expected vs observed)."""
        self.table.append((self.name, what, str(expected), str(observed), ok))
        self.check(f"{what}: expected {expected}, observed {observed}", ok)


# ---- running -----------------------------------------------------------------------

class Runner:
    def __init__(self, emu):
        self.emu = emu
        self.g = Game(emu)
        self.drv = Script(emu)
        self.opt = Options(emu)
        self.cue = CueBlock(emu)
        self.states = States()
        self._plans = {}

    def refresh(self):
        self.g.mods.clear()
        self._plans.clear()
        self.drv.find()
        self.opt.find()
        self.cue.find()

    def record(self, frames, players=(CPU,), each=None, nana=False, extras=False):
        """Advance `frames` frames one at a time; one row per frame with each
        player's fighter (and Nana's with `nana`). each(i, rows) runs before
        frame i. After loading a state saved inside a frame (a running game
        paused) the first advance only finishes that frame; prepare's states
        are saved at a frame's end, so their first advance is a whole frame."""
        emu = self.emu
        emu.h.call("pause")
        ens = {p: self.g.entry(p) for p in players}
        for p in players:
            en = ens[p]
            self.g.modules(en["fighters"][en["active"]])
            if nana and p == CPU:
                self.g.modules(en["fighters"][NANA_INSTANCE])

        def read_row(i):
            row = {"i": i, "drv": self.drv.status()}
            for p in players:
                en = ens[p]
                row[p] = self.g.fighter(en)
                if nana and p == CPU:
                    row["nana"] = self.g.fighter(en, NANA_INSTANCE)
            if extras:
                row["x"] = self.g.extra(ens[CPU])
            return row

        watch = emu.watch(read_row)
        key = (tuple((p, ens[p]["addr"], ens[p]["active"], ens[p]["fighters"][ens[p]["active"]]) for p in players),
               nana, extras)
        if key in self._plans:
            watch.plan = list(self._plans[key])
        try:
            rows = emu.trace(frames, watch, each=each)
            if watch.plan:
                self._plans[key] = list(watch.plan)
        finally:
            emu.h.call("resume")
        return rows

    def load(self, name, options=None, training=None, percent=None):
        """Load state `name` paused; stop the script; write the options
        (fields of cpu::CpuSettings) and the CPU's locked percent."""
        self.states.load(self.emu, name)
        if training is not None:
            self.emu.h.call("resume")
            set_training_option(self.emu, training)
            self.emu.h.call("pause")
        self.drv.stop()
        self.opt.set(**dict({"control": TRAINING}, **(options or {})))
        if percent is not None:
            self.opt.cpu_percent(percent, True)

    def hit_run(self, state, smash, frames, options=None, react=None, script=None, delay=0, percent=None,
                players=(CPU, P1), extras=False, buttons=("A",), hold=5):
        """From `state`: P1 smashes (stick `smash` + A, held 5 frames) after
        `delay` frames. With `script`, the CPU is driven by it (AI off) from
        the start; react(i, hit) may return script steps to start at frame i.
        Returns (rows, hit frame)."""
        self.load(state, options, percent=percent)
        if script is not None:
            self.drv.play(CPU, script, loop=True, ai_off=True)
        hit = [None]

        def each(i, rows):
            if i == delay:
                self.emu.hold(0, buttons=list(buttons), stick=smash)
            elif i == delay + hold:
                self.emu.neutral(0)
            if hit[0] is None and len(rows) > 1:
                a, b = rows[-2][CPU], rows[-1][CPU]
                if b["hitlag"] > a["hitlag"] and (b["hitstun"] > 0 or b["status"] == STATUS["Guard_Damage"]):
                    hit[0] = len(rows) - 1
            if react:
                steps = react(i, hit[0])
                if steps:
                    # (steps, True) plays them in a loop.
                    loop = isinstance(steps, tuple)
                    self.drv.play(CPU, steps[0] if loop else steps, loop=loop, ai_off=True)
        rows = self.record(frames, players, each, extras=extras)
        self.emu.neutral(0)
        if hit[0] is None:
            seen = "; ".join(f"{i}: P1 {q[P1]['x']:.1f} lr {q[P1]['lr']:+.0f} {sname(q[P1]['status'])}, CPU "
                             f"{q[CPU]['x']:.1f} lr {q[CPU]['lr']:+.0f} {sname(q[CPU]['status'])}"
                             for i, q in enumerate(rows) if i in (0, 1, 2, 8, 14) and P1 in q and CPU in q)
            # P1's hitlag tells a hit that never touched the CPU from one that
            # touched it and gave no hitstun.
            lag = max((q[P1]["hitlag"] for q in rows if P1 in q), default=None)
            raise CpuError(f"P1's attack did not hit the CPU (from state {state}, delay {delay}, P1's most "
                           f"hitlag {lag}): {seen}")
        return rows, hit[0]


def training_option(emu):
    return emu.u32(emu.ptr(emu.ptr(nav.IF_MANAGER) + nav.IF_TRAINING_MENU) + LT.CPU_BEHAVIOUR)


def set_training_option(emu, name):
    """Choose Training's own CPU option from its pause menu."""
    want = TRAINING_OPTION[name]
    if training_option(emu) == want:
        return
    nav.pause_training(emu)
    nav.menu_select(emu, 3)
    for _ in range(16):
        if training_option(emu) == want:
            break
        emu.press(0, stick=(228, 128), frames=3, after=8)
    nav.resume_training(emu)
    if training_option(emu) != want:
        raise CpuError(f"Training's CPU option is {training_option(emu)}, wanted {name}")


def driven(rows):
    return [r for r in rows if r["drv"]["driven"] > 0]


def statuses(rows, who=CPU, key=None):
    """[(row index or driven frame, status name)] at each change."""
    out, last = [], None
    for r in rows:
        s = r[who]["status"]
        if s != last:
            out.append((r["drv"]["driven"] if key == "driven" else r["i"], sname(s)))
            last = s
    return out


def first(rows, start, test):
    for k in range(max(start, 0), len(rows)):
        if test(rows[k]):
            return k
    return None


def launch(rows, hit):
    """Shift during hitlag (SDI), launch speed and angle (degrees) from
    frames hit+10..hit+15."""
    x0 = rows[hit][CPU]["x"]
    end = first(rows, hit, lambda r: r[CPU]["hitlag"] == 0)
    a, b = rows[hit + 10][CPU], rows[hit + 15][CPU]
    dx, dy = (b["x"] - a["x"]) / 5, (b["y"] - a["y"]) / 5
    return {"sdi": rows[end - 1][CPU]["x"] - x0 if end else 0.0, "vx": dx, "vy": dy,
            "angle": math.degrees(math.atan2(dy, abs(dx)))}


# ---- match setup ---------------------------------------------------------------------

def prepare(r):
    """CPU on Training's Attack option (an active AI) while the script holds
    it neutral at its spawn; save 'start'. Then the hit states."""
    emu = r.emu
    r.refresh()
    r.drv.play(CPU, [NEUTRAL_STEP], loop=True)
    set_training_option(emu, "Attack")
    emu.h.call("pause")
    emu.step(frames=30)
    r.states.save(emu, "start")
    save_hit_state(r, "far", 21.5)
    save_hit_state(r, "near", 8.5)
    # 'near', the CPU crouching (held down by the script).
    r.load("near")
    r.drv.play(CPU, [CROUCH_STEP], loop=True)
    step_until(emu, "the CPU to crouch", lambda: r.g.fighter(r.g.entry(CPU))["status"] == STATUS["Squat_Wait"], 60)
    r.states.save(emu, "crouch")
    emu.h.call("resume")


def step_until(emu, what, probe, limit):
    """Step the paused game one frame at a time until probe() is true."""
    watch = emu.watch(probe)
    if watch.read():
        return
    emu.advance_until(what, watch, bool, limit)


def save_hit_state(r, name, gap):
    """From 'start': walk the CPU left until it stands `gap` in front of
    Mario, held neutral by the script; save state `name`. The game is
    stepped paused, so the state is the same whatever the host's speed (run
    free, with the harness calls slowed, Peach stopped from 5.7 to 10.1 from
    Mario for a gap of 8.5)."""
    emu = r.emu
    r.states.load(emu, "start")
    cpu, p1 = r.g.entry(CPU), r.g.entry(P1)
    distance = lambda: r.g.fighter(cpu)["x"] - r.g.fighter(p1)["x"]  # noqa: E731
    r.drv.play(CPU, [(0, -0.5, 0.0, 1)], loop=True)
    step_until(emu, "the CPU to walk in", lambda: distance() <= gap + 5.0, 1200)
    r.drv.play(CPU, [NEUTRAL_STEP], loop=True)
    step_until(emu, "the CPU to stand", lambda: r.g.fighter(cpu)["status"] == STATUS["Wait"], 600)
    emu.step(frames=20)
    # Characters walk and brake differently: short steps either way until
    # it stands close enough, or until a wide one cannot come closer (the
    # bodies push apart); then it turns to face Mario again if it must.
    # Each step holds the stick neutral after it until settle() takes the
    # CPU again: a script that ends hands the CPU back to Training's Attack
    # AI, which then dashed and attacked between the steps (Falco moved
    # 10.5 -> 28.4 -> -12.6 over two 4-frame steps; Mario stopped 24.8
    # and 19.5 from a 21.5 gap).
    held = (0, 0.0, 0.0, 600)

    def settle():
        r.drv.play(CPU, [NEUTRAL_STEP], loop=True)
        step_until(emu, "the CPU to stand", lambda: r.g.fighter(cpu)["status"] == STATUS["Wait"], 600)
        emu.step(frames=10)

    blocked, stuck, turned = False, 0, False
    steps = []
    for _ in range(40):
        before = distance()
        if abs(before - gap) <= 2:
            break
        x = -0.4 if before > gap else 0.4
        turned = turned or x > 0
        r.drv.play(CPU, [(0, x, 0.0, 4), held], loop=False)
        emu.step(frames=6)
        settle()
        steps.append(round(distance(), 1))
        stuck = stuck + 1 if before > gap and distance() > before - 0.3 else 0
        if stuck >= 3:
            blocked = True
            break
    if turned:
        r.drv.play(CPU, [(0, -0.4, 0.0, 1), held], loop=False)
        emu.step(frames=3)
        settle()
        steps.append(round(distance(), 1))
    if blocked:
        print(f"  ({name}: the CPU stands {distance():.1f} from Mario, as close as it gets; wanted {gap})")
    elif abs(distance() - gap) > 2:
        raise CpuError(f"the CPU stopped {distance():.1f} from Mario, wanted {gap} (after each step: {steps})")
    r.states.save(emu, name)


# ---- driver scenarios (ported from the prototype) ------------------------------------

def upto(rows, last):
    """The rows to the first one of driven frame `last`: a state saved at a
    frame's end gives one driven frame more in the same number of rows than
    one saved inside a frame."""
    end = next((k for k, x in enumerate(rows) if x["drv"]["driven"] >= last), len(rows) - 1)
    return rows[:end + 1]


def once_per_frame(rows):
    d = [r["drv"]["driven"] for r in rows]
    return all(b - a == 1 for a, b in zip(d, d[1:])) and all(r["drv"]["calls"] == 1 for r in rows)


def c1_shield(r):
    res = Result("c1 hold shield")
    r.load("start")
    r.drv.play(CPU, [(GUARD, 0.0, 0.0, 300)])
    rows = driven(r.record(181))
    res.note(f"statuses by driven frame: {statuses(rows, key='driven')}")
    on = [x["drv"]["driven"] for x in rows if x[CPU]["status"] == STATUS["Guard_On"]]
    held = [x["drv"]["driven"] for x in rows if x[CPU]["status"] == STATUS["Guard"]]
    res.row("Guard_On on driven frame 1, Guard to the end", "1 / all", f"{on[:1]} / {len(on) + len(held)}"
            f" of {len(rows)}", on and on[0] == 1 and held and len(on) + len(held) == len(rows))
    res.row("driven calls per game frame", 1, sorted({x['drv']['calls'] for x in rows}), once_per_frame(rows))
    return res


def c2_jump(r, every=40):
    res = Result(f"c2 jump every {every}")
    r.load("start")
    r.drv.play(CPU, [(JUMP, 0.0, 0.0, 1), (0, 0.0, 0.0, every - 1)], loop=True)
    rows = upto(driven(r.record(every * 5 + 1)), every * 5)
    squats = [b["drv"]["driven"] for a, b in zip(rows, rows[1:])
              if b[CPU]["status"] == STATUS["Jump_Squat"] != a[CPU]["status"]]
    if rows[0][CPU]["status"] == STATUS["Jump_Squat"]:
        squats.insert(0, rows[0]["drv"]["driven"])
    res.row("jump squat starts (driven frames)", [1 + every * k for k in range(5)], squats,
            squats == [1 + every * k for k in range(5)])
    r.drv.stop()
    return res


def c3_sequence(r):
    res = Result("c3 shield 10, release 20, jab")
    r.load("start")
    r.drv.play(CPU, [(GUARD, 0.0, 0.0, 10), (0, 0.0, 0.0, 20), (A, 0.0, 0.0, 1), (0, 0.0, 0.0, 60)])
    rows = driven(r.record(61))
    by = {x["drv"]["driven"]: sname(x[CPU]["status"]) for x in rows}
    at = lambda name: min((d for d, s in by.items() if s == name), default=None)  # noqa: E731
    res.note(f"statuses: {statuses(rows, key='driven')}")
    res.row("Guard_On / Guard_Off / Attack (driven frame)", "1 / 11 / 31",
            f"{at('Guard_On')} / {at('Guard_Off')} / {at('Attack')}",
            (at("Guard_On"), at("Guard_Off"), at("Attack")) == (1, 11, 31))
    return res


def c4_stick(r):
    res = Result("c4 stick left 60, right 60")
    r.load("start")
    r.drv.play(CPU, [(0, -1.0, 0.0, 60), (0, 1.0, 0.0, 60), (0, 0.0, 0.0, 30)])
    rows = upto(driven(r.record(151)), 150)
    xs =[x[CPU]["x"] for x in rows]
    res.note(f"statuses: {statuses(rows, key='driven')}")
    res.row("x at driven 1/60/120/150", "left then right",
            f"{xs[0]:.1f} {xs[59]:.1f} {xs[119]:.1f} {xs[-1]:.1f}",
            xs[59] < xs[0] - 40 and xs[119] > xs[59] + 40)
    res.row("Dash on driven frame 1, faces right at the end", "Dash, +1",
            f"{sname(rows[0][CPU]['status'])}, {rows[-1][CPU]['lr']:+.0f}",
            rows[0][CPU]["status"] == STATUS["Dash"] and rows[-1][CPU]["lr"] > 0)
    return res


WALK_OFF = [(0, 1.0, 0.0, 45)]


def c5_silence(r):
    """Off-stage: the script walks the CPU off, then writes neutral for 600
    frames with the AI off; baselines without the script."""
    res = Result("c5 silence (script)")
    died, tried = {}, {}
    for label, option, silence in (("script neutral, AI on Attack", None, True),
                                   ("no script, AI on Attack", None, False),
                                   ("no script, Training Stop", "Stop", False),
                                   ("no script, Training Control", "Control", False)):
        r.load("start", training=option)
        r.drv.play(CPU, WALK_OFF + ([(0, 0.0, 0.0, 600)] if silence else []), ai_off=silence)
        presses, flags = set(), set()

        def watch(i, rows):
            if silence and rows and rows[-1]["drv"]["driven"] > 45:
                presses.add(r.drv.status()["ai_buttons"] & 0x7FFF)
                flags.add(r.g.ai_flag(1))
        rows = r.record(46 + 600, each=watch)
        seen = {sname(x[CPU]["status"]) for x in rows}
        died[label] = any(x[CPU]["status"] in GONE for x in rows)
        # A second jump or an up special (P+ ids from 0x112) is the AI
        # trying to recover.
        tried[label] = any(x[CPU]["status"] == STATUS["Jump_Aerial"] or x[CPU]["status"] >= 0x112
                           for x in rows if x[CPU]["status"] not in GONE)
        res.note(f"{label}: KO'd {died[label]}; statuses {sorted(seen)}")
        if silence:
            res.note(f"  AI output while silenced (discarded): {sorted(hex(b) for b in presses)}; "
                     f"AI flag values {sorted(flags)}")
            res.row("silenced: no jump or recovery special", "none",
                    sorted(seen & {"Jump_Aerial", "Fall_Special"}), not seen & {"Jump_Aerial", "Fall_Special"})
        r.drv.stop()
    res.row("tries to recover: script / Attack / Stop / Control", "no / yes / yes / no",
            " / ".join("yes" if tried[k] else "no" for k in tried),
            [tried[k] for k in tried] == [False, True, True, False])
    # Whether the AI's recovery then makes it back depends on where its
    # randomness stands in the saved state; the KO is noted, not checked.
    res.note("KO'd: " + ", ".join(f"{k}: {'yes' if died[k] else 'no'}" for k in died))
    res.row("KO'd: script / Control", "yes / yes",
            f"{'yes' if died['script neutral, AI on Attack'] else 'no'} / "
            f"{'yes' if died['no script, Training Control'] else 'no'}",
            died["script neutral, AI on Attack"] and died["no script, Training Control"])
    return res


def e_human(r):
    res = Result("e P1 playback")
    r.load("start", training="Stop")
    r.drv.play(P1, [(0, 1.0, 0.0, 30), (JUMP, 0.0, 0.0, 1), (0, 0.0, 0.0, 30)])
    rows = driven(r.record(61, players=(P1, CPU)))
    xs = [x[P1]["x"] for x in rows]
    squat = [x["drv"]["driven"] for x in rows if x[P1]["status"] == STATUS["Jump_Squat"]]
    res.note(f"statuses: {statuses(rows, P1, key='driven')}")
    res.row("P1: Dash on 1, Jump_Squat from 31, airborne by 40", "Dash / 31 / yes",
            f"{sname(rows[0][P1]['status'])} / {squat[:1]} / {max(x[P1]['y'] for x in rows[35:]) > 5}",
            rows[0][P1]["status"] == STATUS["Dash"] and squat[:1] == [31] and xs[29] > xs[0] + 30
            and max(x[P1]["y"] for x in rows[35:]) > 5 and not any(x[P1]["status"] in HIT for x in rows))
    return res


def d_di(r):
    """Fox at 60% forward-smashed; the script holds a stick from the hit on."""
    res = Result("di (script)")
    out = {}
    for label, ai_off, sticks in (("AI on", False, ("neutral", "left", "right")),
                                  ("AI off", True, tuple(STICKS))):
        for stick in sticks:
            sx, sy = STICKS[stick]
            r.load("far", percent=60)
            r.drv.play(CPU, [NEUTRAL_STEP], loop=True, ai_off=ai_off)
            hit = [None]

            def each(i, rows):
                if i == 0:
                    r.emu.hold(0, buttons=["A"], stick=FSMASH)
                elif i == 5:
                    r.emu.neutral(0)
                if hit[0] is None and rows and rows[-1][CPU]["hitlag"] > 0:
                    hit[0] = len(rows) - 1
                    r.drv.play(CPU, [(0, sx, sy, 400)], ai_off=ai_off)
            rows = r.record(60, each=each)
            if hit[0] is None:
                raise CpuError("the forward smash missed")
            ln = launch(rows, hit[0])
            ln["down"] = any(x[CPU]["status"] == STATUS["Down"] for x in rows[hit[0]:hit[0] + 20])
            out[(label, stick)] = ln
            res.note(f"{label:7s} {stick:7s}: shift in hitlag {ln['sdi']:+.2f}, launch ({ln['vx']:.2f}, "
                     f"{ln['vy']:.2f}) {ln['angle']:.1f} deg{' Down' if ln['down'] else ''}")
            r.drv.stop()
    same = lambda a, b: abs(a["vx"] - b["vx"]) < 0.01 and abs(a["vy"] - b["vy"]) < 0.01  # noqa: E731
    on = {s: out[("AI on", s)] for s in ("neutral", "left", "right")}
    off = {s: out[("AI off", s)] for s in STICKS}
    res.row("AI flag set: stick changes the launch", "no", "no" if same(on["neutral"], on["left"])
            and same(on["neutral"], on["right"]) else "yes",
            same(on["neutral"], on["left"]) and same(on["neutral"], on["right"]))
    res.row("AI flag cleared: angle right < neutral < left, up", "yes",
            f"{off['right']['angle']:.1f} < {off['neutral']['angle']:.1f} < {off['left']['angle']:.1f}, "
            f"{off['up']['angle']:.1f}",
            off["right"]["angle"] < off["neutral"]["angle"] - 5 < off["left"]["angle"] - 10
            and off["up"]["angle"] > off["neutral"]["angle"] + 5)
    return res


def d_tech(r):
    """Fox at 50% down-smashed (lands in hitstun, AI off); guard one frame k
    frames before the landing frame."""
    res = Result("tech (script)")
    landing = [None]

    def run(k=None, stick=0.0, extra=None):
        def react(i, hit):
            if hit is None or landing[0] is None:
                return None
            land = hit + landing[0]
            if k is not None and i == land - k:
                if k == 0:
                    return [(GUARD, stick, 0.0, 1), (0, stick, 0.0, 300)]
                return [(GUARD, 0.0, 0.0, 1)] + ([(0, 0.0, 0.0, k - 1)] if k > 1 else []) + [(0, stick, 0.0, 300)]
            if extra is not None and i == land - extra:
                return [(GUARD, 0.0, 0.0, 1), (0, 0.0, 0.0, 300)]
            return None
        rows, hit = r.hit_run("near", DSMASH, 120, react=react, script=[NEUTRAL_STEP], percent=50)
        after = first(rows, hit + 1, lambda x: x[CPU]["status"] not in FLYING)
        return sname(rows[after][CPU]["status"]), after - hit, rows[-1][CPU]["x"] - rows[after][CPU]["x"]

    status, landing[0], _ = run()
    res.note(f"no press: {status}, {landing[0]} frames after the hit")
    got = {}
    for k in (0, 19, 20):
        got[k] = run(k)[0]
    res.row("guard k frames before landing, k = 0 / 19 / 20", "Passive / Passive / Down",
            " / ".join(got[k] for k in (0, 19, 20)), [got[k] for k in (0, 19, 20)] == ["Passive", "Passive", "Down"])
    for stick, word in ((1.0, "right (away)"), (-1.0, "left (toward)")):
        s, _, moved = run(5, stick)
        res.row(f"guard 5 before, stick {word} from landing", "Passive_Fb that way", f"{s}, x {moved:+.1f}",
                s == "Passive_Fb" and moved * stick > 10)
    s, _, _ = run(5, extra=25)
    res.row("extra guard 25 before, then 5 before", "Down (lockout)", s, s == "Down")
    return res


# ---- CPU Options scenarios -------------------------------------------------------------

def on(**kw):
    """Options on, with the scenario's values over neutral defaults."""
    base = {"control": LAB, "behavior": BEHAVIOR["Stand"], "tdi": TDI["None"], "sdiAmount": 0, "sdiDir": SDI["Auto"],
            "counterGround": 0, "counterAir": 0, "counterShield": 0, "counterDelay": 0,
            "infiniteShields": SHIELDS["Off"], "shieldHealth": 0, "intangible": 0, "tech": TECH["None"],
            "getup": GETUP["Stand"], "techChance": [25, 25, 25, 25], "getupChance": [25, 25, 25, 25],
            "asdi": 0, "shieldAngle": 0, "grabEscape": 0, "grabRelease": 0, "controlledBy": 0, "techTrap": 0,
            "techLockout": 0, "missTechWait": 0, "recovery": RECOVERY["Off"],
            "ledgeOption": 0, "clatterEscape": 0, "adaptiveMode": 0, "dualStickSdi": 0,
            "autoLCancel": 1, "shfflTiming": 0}
    base.update(kw)
    return base


def o_shield(r):
    res = Result("Behavior Shield")
    r.load("near", on(behavior=BEHAVIOR["Shield"], counterShield=SHIELD_ACTIONS["None"]), percent=0)
    flags = []

    def each(i, rows):
        if i == 60:
            r.emu.hold(0, buttons=["A"])
        elif i == 63:
            r.emu.neutral(0)
        flags.append(r.g.ai_flag(1))
    rows = r.record(180, each=each)
    st = statuses(rows)
    res.note(f"statuses: {st}")
    names = [n for i, n in st if i > 0 or n == "Guard_On"]
    # The same load with a script holding guard from its first frame (AI
    # off): Behavior Shield must shield on the same row, not later.
    r.load("near", percent=0)
    r.drv.play(CPU, [(GUARD, 0.0, 0.0, 300)], ai_off=True)
    srows = r.record(20)
    r.drv.stop()
    script_on = first(srows, 0, lambda x: x[CPU]["status"] == STATUS["Guard_On"])
    brain_on = first(rows, 0, lambda x: x[CPU]["status"] == STATUS["Guard_On"])
    res.row("first Guard_On row: Behavior Shield / a script holding guard from the load", "the same",
            f"{brain_on} / {script_on}", brain_on is not None and brain_on == script_on)
    stun = first(rows, 0, lambda x: x[CPU]["status"] == STATUS["Guard_Damage"])
    back = first(rows, stun or 0, lambda x: x[CPU]["status"] == STATUS["Guard"]) if stun else None
    res.row("shield before the jab", "Guard_On -> Guard", names[:2], names[:2] == ["Guard_On", "Guard"])
    res.row("jab on shield, then shield again", "Guard_Damage -> Guard, held to the end",
            f"{sname(rows[stun][CPU]['status']) if stun else None} at {stun} -> Guard at {back}, "
            f"last {sname(rows[-1][CPU]['status'])}",
            stun is not None and back is not None and all(x[CPU]["status"] == STATUS["Guard"] for x in rows[back:]))
    res.row("AI flag while driven", "0", sorted(set(flags[2:])), set(flags[2:]) == {0})
    shield0 = [x[CPU]["shield"] for x in rows[10:58]]
    res.note(f"shield health while holding (Infinite Shields Off): {shield0[0]:.2f} -> {shield0[-1]:.2f}")
    res.row("Infinite Shields Off: health drains", "falls", f"{shield0[0]:.2f} -> {shield0[-1]:.2f}",
            shield0[-1] < shield0[0] - 1)
    for mode, health in (("On", 30), ("Until Hit", 45)):
        r.load("near", on(behavior=BEHAVIOR["Shield"], infiniteShields=SHIELDS[mode], shieldHealth=health),
               percent=0)
        rows = r.record(90)
        vals = [x[CPU]["shield"] for x in rows[10:]]
        res.row(f"Infinite Shields {mode} {health}: health while holding", f"{health} (-1 frame of drain)",
                f"{min(vals):.2f}..{max(vals):.2f}", health - 1 <= min(vals) and max(vals) <= health + 0.01)
    return res


def ground_probe_guard(r):
    def react(i, hit):
        return [(GUARD, 0.0, 0.0, 300)] if hit is not None and i == hit + GROUND_FREE else None
    rows, hit = ground_hit(r, 90, react=react)
    g = first(rows, hit + 1, lambda x: x[CPU]["status"] in (STATUS["Guard_On"], STATUS["Guard"]))
    return rows, hit, g


def o_counter_ground(r):
    res = Result("Counter Action (Ground)")
    rows, hit, act = ground_probe_guard(r)
    if act is None:
        raise CpuError("the guard probe never shielded")
    stun = [(x["i"] - hit, x[CPU]["hitlag"], x[CPU]["hitstun"], sname(x[CPU]["status"])) for x in rows[hit:act + 1]]
    res.note(f"probe: hit at {hit}, Guard_On at {act} (+{act - hit}); (frame, hitlag, hitstun, status): {stun}")
    # Up smash probe: the C-stick up smash input every other frame from the
    # hit (two runs a frame apart); the first Attack_Hi4_Start is the first
    # frame an up smash can start.
    usmash = []
    for phase in (0, 1):
        def react(i, h, phase=phase):
            if h is not None and i == h + GROUND_FREE + phase:
                return ([(0x121, 0.0, 1.0, 1), (0, 0.0, 0.0, 1)], True)
            return None
        prow, ph = ground_hit(r, 60, react=react)
        u = first(prow, ph + 1, lambda x: x[CPU]["status"] == STATUS["Attack_Hi4_Start"])
        if u is not None:
            usmash.append(u - ph)
    res.note(f"up smash probe: first Attack_Hi4_Start at +{usmash} (phases 0, 1)")
    for d in (0, 5):
        rows, hit2 = ground_hit(r, 90, on(counterGround=GROUND["Up Smash"], counterDelay=d))
        u = first(rows, hit2 + 1, lambda x: x[CPU]["status"] == STATUS["Attack_Hi4_Start"])
        res.note(f"delay {d}: statuses {statuses(rows)[:8]}")
        # Delay d: d actionable frames (from the guard probe's frame) pass
        # first; the up smash cannot start before the up smash probe's frame.
        want = max(act - hit + d, min(usmash)) if usmash else None
        res.row(f"Up Smash, delay {d}: first Attack_Hi4_Start (frames after the hit)", want,
                None if u is None else u - hit2, u is not None and u - hit2 == want)
    return res


def o_counter_delay_land(r):
    """Counter Delay resets on non-actionable frames.
    With Counter Delay 10 on a hit whose hitstun ends in the air a few frames
    before Landing, the grounded counter must come 10 actionable frames after
    Landing ends, not 10 minus the pre-landing air frames."""
    res = Result("Counter Delay across Landing")
    en = r.g.entry(CPU)

    def run_lifted(lift, options=None, script=None, guard_at=None):
        lifted = [False]
        def react(i, h):
            if h is not None and not lifted[0]:
                f = r.g.fighter(en)
                if f["hitlag"] == 0:
                    put_fighter(r, en, f["x"], f["y"] + lift)
                    lifted[0] = True
            if guard_at is not None and h is not None and i == guard_at:
                return [(GUARD, 0.0, 0.0, 60)]
            return None
        return r.hit_run("near", USMASH, 140, options=options, react=react, script=script, percent=0)

    pick = None
    seen = []
    for lift in (12.0, 18.0, 24.0, 30.0, 36.0, 42.0):
        rows, hit = run_lifted(lift, script=[NEUTRAL_STEP])
        e = first(rows, hit + 1, lambda x: x[CPU]["hitstun"] == 0 and x[CPU]["hitlag"] == 0)
        land = first(rows, e + 1, lambda x: not x[CPU]["air"]) if e is not None else None
        st_h = sname(rows[hit + 1][CPU]["status"]) if hit + 1 < len(rows) else None
        st_e = sname(rows[e][CPU]["status"]) if e is not None else None
        st_l = sname(rows[land][CPU]["status"]) if land is not None else None
        gap = (land - e) if (land is not None and e is not None) else None
        seen.append((lift, st_h, st_e, st_l, gap))
        if e is not None and rows[e][CPU]["air"] \
                and rows[e][CPU]["status"] in (STATUS["Damage_Air"], STATUS["Fall"]) \
                and land is not None and rows[land][CPU]["status"] in (STATUS["Landing"], STATUS["Landing_Light"]) \
                and 3 <= land - e <= 7:
            pick = (lift, hit, e, land)
            break
    if pick is None:
        raise CpuError(f"no lift left hitstun ending 3..7 frames before Landing (tried {seen})")
    lift, hit0, e0, land0 = pick
    res.note(f"setup: USMASH 0% lift +{lift:.0f}, hit {hit0}, hitstun end {e0} ({sname(rows[e0][CPU]['status'])}), "
             f"land {land0} ({land0 - e0} air actionable frames before Landing)")
    # Guard probe from the landing frame to find the first actionable frame after Landing.
    prow, ph = run_lifted(lift, script=[NEUTRAL_STEP], guard_at=land0)
    act = first(prow, land0 + 1, lambda x: x[CPU]["status"] in (STATUS["Guard_On"], STATUS["Guard"]))
    if act is None:
        raise CpuError("guard probe never shielded after Landing")
    res.note(f"guard probe: Landing {land0}, Guard_On at {act} (+{act - land0} after land)")
    delay = 10
    rows, hit2 = run_lifted(lift, options=on(counterGround=GROUND["Up Smash"],
                                             counterAir=AIR_ACTIONS["Airdodge"], counterDelay=delay))
    u = first(rows, land0, lambda x: x[CPU]["status"] == STATUS["Attack_Hi4_Start"])
    res.note(f"delay {delay}: statuses {statuses(rows[hit2:])[:10]}; Attack_Hi4_Start at {u} "
             f"({None if u is None else u - act} after Guard_On)")
    res.row("Counter Delay 10 across Landing: Attack_Hi4_Start after Landing ends",
            f"+{delay} after Guard_On (row {act + delay})",
            f"+{None if u is None else u - act} (row {u})",
            u is not None and u - act == delay)
    return res


def o_counter_delay_tumble(r):
    """With Counter Delay > 0 in tumble, the CPU waiting in kCounter must still
    tech before landing and get up on a missed tech."""
    res = Result("Counter Delay in Tumble")
    en = r.g.entry(CPU)

    def run_tumble(move, pct, lift, options=None, script=None, frames=200):
        lifted = [False]
        def react(i, h):
            if lift > 0.0 and h is not None and not lifted[0]:
                f = r.g.fighter(en)
                if f["hitlag"] == 0:
                    put_fighter(r, en, f["x"], f["y"] + lift)
                    lifted[0] = True
            return None
        return r.hit_run("near", move, frames, options=options, react=react, script=script, percent=pct)

    pick = None
    seen = []
    candidates = [(USMASH, p, 0.0) for p in (25, 30, 35, 40, 45, 50)] + [
        (DSMASH, 50, l) for l in (12.0, 18.0, 24.0, 30.0, 36.0)
    ]
    for move, pct, lift in candidates:
        rows, hit = run_tumble(move, pct, lift, script=[NEUTRAL_STEP], frames=160)
        e = first(rows, hit + 1, lambda x: x[CPU]["hitstun"] == 0 and x[CPU]["hitlag"] == 0)
        land = first(rows, e + 1, lambda x: not x[CPU]["air"]) if e is not None else None
        st_e = sname(rows[e][CPU]["status"]) if e is not None else None
        st_l = sname(rows[land][CPU]["status"]) if land is not None else None
        gap = (land - e) if (land is not None and e is not None) else None
        seen.append((move, pct, lift, st_e, st_l, gap))
        if e is not None and rows[e][CPU]["air"] and rows[e][CPU]["status"] in TUMBLE \
                and land is not None and rows[land][CPU]["status"] == STATUS["Down"] \
                and 6 <= land - e <= 9:
            pick = (move, pct, lift, hit, e, land)
            break
    if pick is None:
        raise CpuError(f"no tumble hit ended hitstun 6..9 frames before Down (tried {seen})")
    move, pct, lift, hit0, e0, land0 = pick
    res.note(f"setup: move {move} at {pct}% lift +{lift:.0f}, hit {hit0}, hitstun end {e0} "
             f"({sname(rows[e0][CPU]['status'])}), land {land0} ({land0 - e0} frames before Down)")
    delay = 10

    # Case 1: Tech In Place (Latest, kTechLead=5 < land-e) during Counter Delay 10 -> must tech.
    t_rows, _ = run_tumble(move, pct, lift,
                           options=on(tech=TECH["In Place"], techLockout=1,
                                      counterGround=GROUND["Up Smash"], counterAir=AIR_ACTIONS["None"],
                                      counterDelay=delay), frames=200)
    t_land = first(t_rows, e0, lambda x: not x[CPU]["air"])
    t_st = sname(t_rows[t_land][CPU]["status"]) if t_land is not None else None
    t_u = first(t_rows, t_land or e0, lambda x: x[CPU]["status"] == STATUS["Attack_Hi4_Start"])
    res.note(f"tech case: land {t_land} ({t_st}), Up Smash {t_u}, statuses {statuses(t_rows[e0:])[:10]}")
    res.row("Counter Delay 10 in tumble, Tech In Place (Latest): techs and counters",
            "Passive, then Attack_Hi4_Start", f"{t_st}, Up Smash {t_u}",
            t_st == "Passive" and t_u is not None)

    # Case 2: Tech None, Get Up Stand during Counter Delay 10 -> must get up promptly and counter.
    g_rows, _ = run_tumble(move, pct, lift,
                           options=on(tech=TECH["None"], getup=GETUP["Stand"],
                                      counterGround=GROUND["Up Smash"], counterAir=AIR_ACTIONS["None"],
                                      counterDelay=delay), frames=220)
    g_land = first(g_rows, e0, lambda x: not x[CPU]["air"])
    down_wait = sum(1 for x in g_rows[g_land:] if x[CPU]["status"] == STATUS["Down_Wait"]) if g_land is not None else None
    stand = first(g_rows, g_land or e0, lambda x: x[CPU]["status"] == STATUS["Down_Stand"])
    g_u = first(g_rows, stand or (g_land or e0), lambda x: x[CPU]["status"] == STATUS["Attack_Hi4_Start"])
    res.note(f"getup case: land {g_land}, Down_Wait {down_wait}f, Down_Stand {stand}, Up Smash {g_u}, "
             f"statuses {statuses(g_rows[e0:])[:10]}")
    res.row("Counter Delay 10 in tumble, missed tech Get Up Stand: gets up and counters",
            "Down_Wait <= 4, Down_Stand, Attack_Hi4_Start",
            f"Down_Wait {down_wait}, stand {stand}, Up Smash {g_u}",
            down_wait is not None and down_wait <= 4 and stand is not None and g_u is not None)
    return res


def o_counter_shield(r):
    res = Result("Counter Action (Shield)")
    out = {}
    for name in ("None", "Grab"):
        r.load("near", on(behavior=BEHAVIOR["Shield"], counterShield=SHIELD_ACTIONS[name]), percent=0)

        def each(i, rows):
            if i == 40:
                r.emu.hold(0, buttons=["A"])
            elif i == 43:
                r.emu.neutral(0)
        rows = r.record(120, each=each)
        stun = first(rows, 0, lambda x: x[CPU]["status"] == STATUS["Guard_Damage"])
        end = first(rows, stun or 0, lambda x: x[CPU]["status"] != STATUS["Guard_Damage"]) if stun else None
        out[name] = (stun, end, sname(rows[end][CPU]["status"]) if end else None)
        res.note(f"{name}: {statuses(rows)[:8]}")
    res.row("shield stun ends: next status with None / Grab", "Guard / Catch, same frame",
            f"{out['None'][2]} at {out['None'][1]} / {out['Grab'][2]} at {out['Grab'][1]}",
            out["None"][2] == "Guard" and out["Grab"][2] == "Catch" and out["None"][1] == out["Grab"][1])
    return res


def o_shield_slideoff(r):
    """A shielding CPU pushed off a ledge by shield pushback leaves shield into
    the air and must not stay in shield stun or run its out-of-shield action in
    the air (e.g. air dodge, aerial, or retrying an OOS script offstage)."""
    res = Result("Shield slide-off")
    en = r.g.entry(CPU)
    # Put CPU shielding right beside FD's right edge facing away from the stage
    # (so shield pushback slides it offstage into Damage_Fall) with Recovery Lab
    # and an OOS counter (Spotdodge, Grab): Mario's forward smash pushes it
    # offstage in Guard_Damage.
    for label, action in (("Spotdodge", 4), ("Grab", 1)):
        r.load("far", on(behavior=BEHAVIOR["Shield"], counterShield=action, recovery=RECOVERY["PPlusAI"]), percent=0)
        put_fighter(r, r.g.entry(P1), 74.0, 0.0)
        put_fighter(r, en, 84.0, 0.0)
        post = r.g.modules(en["fighters"][en["active"]])["posture"]
        r.emu.h.call("write_mem", addr=post + FACING, hex=struct.pack(">f", 1.0).hex())
        pos = len(r.emu.log_text())

        def each(i, rows):
            if i == 25:
                r.emu.hold(0, buttons=["A"], stick=FSMASH)
            elif i == 30:
                r.emu.neutral(0)
        rows = r.record(240, players=(CPU, P1), each=each)
        r.emu.neutral(0)
        stun = first(rows, 0, lambda x: x[CPU]["status"] == STATUS["Guard_Damage"])
        air = first(rows, stun, lambda x: x[CPU]["air"]) if stun is not None else None
        after_air = rows[air:air + 60] if air is not None else []
        bad = sorted({sname(x[CPU]["status"]) for x in after_air
                      if x[CPU]["status"] in (STATUS["Escape_Air"], STATUS["Attack_Air"])})
        ko = any(x[CPU]["status"] in GONE for x in rows[air:]) if air is not None else False
        log_lines = [ln for ln in r.emu.log_text()[pos:].splitlines() if "counter list" in ln]
        res.note(f"OOS {label}: stun {stun}, air {air}, bad {bad}, KO {ko}, counter logs {log_lines}; "
                 f"statuses {statuses(rows)[:16]}")
        res.row(f"pushed offstage with OOS {label}: no airborne OOS action or KO",
                "airborne, no Escape_Air/Attack_Air, no OOS counter, no KO",
                f"air {air}, bad {bad}, logs {len(log_lines)}, KO {ko}",
                stun is not None and air is not None and not bad and not log_lines and not ko)
    return res


def o_shield_oos_retry(r):
    """Out-of-shield script retry must keep guard held instead of dropping into
    Guard_Off (15 frames), and Counter Delay out of shield must add exactly N
    frames after the delay-0 OOS counter frame."""
    res = Result("OOS retry and delay")
    # Shield Angle Down + OOS Spotdodge: shieldTilt holds stick down during
    # Guard/Guard_Damage, so the first Spotdodge attempt has no fresh flick and
    # kAfter retries the script. Retry must reset the stick while keeping guard
    # held (no Guard_Off).
    r.load("near", on(behavior=BEHAVIOR["Shield"], shieldAngle=ANGLE["Down"], counterShield=4), percent=0)

    def each_jab(i, rows):
        if i == 30:
            r.emu.hold(0, buttons=["A"])
        elif i == 33:
            r.emu.neutral(0)
    rows = r.record(120, each=each_jab)
    r.emu.neutral(0)
    stun = first(rows, 0, lambda x: x[CPU]["status"] == STATUS["Guard_Damage"])
    guard_off = first(rows, stun, lambda x: x[CPU]["status"] == STATUS["Guard_Off"]) if stun is not None else None
    esc = first(rows, stun, lambda x: x[CPU]["status"] == STATUS["Escape"]) if stun is not None else None
    res.note(f"Shield Angle Down + OOS Spotdodge: stun {stun}, Guard_Off {guard_off}, Escape {esc}; "
             f"statuses {statuses(rows)[:14]}")
    res.row("OOS Spotdodge retry with Shield Angle Down: no Guard_Off, enters Escape",
            "Escape, no Guard_Off", f"Escape {esc}, Guard_Off {guard_off}",
            stun is not None and esc is not None and guard_off is None)

    # Counter Delay out of shield: compare Grab with delay 0 vs delay 5 against
    # the Guard_Damage -> Guard transition frame.
    got = {}
    for d in (0, 5):
        drows, dstun, dend = shield_run(r, on(behavior=BEHAVIOR["Shield"], counterShield=1, counterDelay=d))
        catch = first(drows, dstun, lambda x: x[CPU]["status"] == STATUS["Catch"]) if dstun is not None else None
        got[d] = (dstun, dend, catch, None if catch is None or dstun is None else catch - dstun)
        res.note(f"OOS Grab delay {d}: stun {dstun}, end {dend}, Catch {catch} (+{got[d][3]}); "
                 f"statuses {statuses(drows)[:10]}")
    diff = got[5][3] - got[0][3] if got[0][3] is not None and got[5][3] is not None else None
    res.row("OOS Grab Counter Delay 5 vs 0: extra frames", 5,
            f"{diff} (+{got[5][3]} vs +{got[0][3]})", diff == 5)
    return res


def o_counter_air(r):
    res = Result("Counter Action (Air)")
    # Probe: guard with the stick toward Mario (left), tapped every other
    # frame from the hit (two runs a frame apart): the first Escape_Air is
    # the first frame an airdodge can start.
    firsts = []
    for phase in (0, 1):
        def react(i, hit, phase=phase):
            if hit is not None and i == hit + 1 + phase:
                return ([(GUARD, -1.0, 0.0, 1), (0, 0.0, 0.0, 1)], True)
            return None
        rows, hit = r.hit_run("far", FSMASH, 160, react=react, script=[NEUTRAL_STEP], percent=60)
        e = first(rows, hit + 1, lambda x: x[CPU]["status"] == STATUS["Escape_Air"])
        if e is not None:
            firsts.append(e - hit)
    rows, hit = r.hit_run("far", FSMASH, 160, on(counterAir=AIR_ACTIONS["Airdodge"]), percent=60)
    e = first(rows, hit + 1, lambda x: x[CPU]["status"] == STATUS["Escape_Air"])
    h0 = first(rows, hit + 1, lambda x: x[CPU]["hitstun"] == 0 and x[CPU]["hitlag"] == 0)
    if h0:
        res.note("rows around the end of hitstun (frame after the hit, hitstun, status, x, y): "
                 + str([(x["i"] - hit, x[CPU]["hitstun"], sname(x[CPU]["status"]), round(x[CPU]["x"], 1),
                         round(x[CPU]["y"], 1)) for x in rows[h0 - 3:h0 + 4]]))
        res.note(f"Mario at x {rows[h0][P1]['x']:.1f}")
    res.note(f"options: statuses {statuses(rows)[:8]}; hitstun over at +{h0 - hit if h0 else None}")
    res.row("Airdodge: first Escape_Air (frames after the hit)", f"probe {min(firsts) if firsts else None}",
            None if e is None else e - hit, e is not None and firsts and e - hit == min(firsts))
    return res


def o_tdi(r):
    res = Result("Trajectory DI")
    ang = {}
    for name in ("Inwards", "None", "Outwards"):
        rows, hit = r.hit_run("far", FSMASH, 40, on(tdi=TDI[name]), percent=60)
        ang[name] = launch(rows, hit)
        res.note(f"{name}: launch ({ang[name]['vx']:.2f}, {ang[name]['vy']:.2f}) {ang[name]['angle']:.1f} deg")
    res.row("launch angle Inwards > None > Outwards", "yes",
            f"{ang['Inwards']['angle']:.1f} / {ang['None']['angle']:.1f} / {ang['Outwards']['angle']:.1f}",
            ang["Inwards"]["angle"] > ang["None"]["angle"] + 2 > ang["Outwards"]["angle"] + 4)
    return res


def o_sdi(r):
    res = Result("Smash DI")
    shift = {}
    for n in (0, 1, 3):
        rows, hit = r.hit_run("far", FSMASH, 40, on(sdiAmount=n, sdiDir=SDI["Left"]), percent=60)
        ln = launch(rows, hit)
        shift[n] = -ln["sdi"]
        res.note(f"amount {n}: hitlag {rows[hit][CPU]['hitlag']} frames, shift left in hitlag {shift[n]:+.2f}")
    one = shift[1] - shift[0]
    res.row("shift left in hitlag, amount 0 / 1 / 3", "0 / d / 3d (d > 1)",
            f"{shift[0]:+.2f} / {shift[1]:+.2f} / {shift[3]:+.2f}",
            abs(shift[0]) < 0.5 and one > 1 and abs((shift[3] - shift[0]) - 3 * one) < 0.75 * one)
    return res


def o_dual_stick_sdi(r):
    """CPU Dual-Stick SDI (dualStickSdi=1): triggers consecutive-frame SDI
    shifts on hitlag > 1 (more shifts in the same hitlag window than
    single-stick dualStickSdi=0), avoids downward Y when grounded on horizontal
    SDI, and preserves final-hitlag ASDI and post-hitlag TDI."""
    res = Result("Dual-Stick SDI")

    def sdi_jumps(rows, hit):
        end = first(rows, hit + 1, lambda x: x[CPU]["hitlag"] == 0)
        if end is None:
            raise CpuError("hitlag did not end")
        jumps = []
        for k in range(hit + 1, end):
            dx = rows[k][CPU]["x"] - rows[k - 1][CPU]["x"]
            dy = rows[k][CPU]["y"] - rows[k - 1][CPU]["y"]
            if math.hypot(dx, dy) > 1.0:
                jumps.append((k - hit, round(dx, 2), round(dy, 2)))
        return jumps, end

    rows_s, hit_s = r.hit_run("far", FSMASH, 45,
                              on(sdiAmount=7, sdiDir=SDI["Left"], dualStickSdi=0), percent=60)
    jumps_s, end_s = sdi_jumps(rows_s, hit_s)
    rows_d, hit_d = r.hit_run("far", FSMASH, 45,
                              on(sdiAmount=7, sdiDir=SDI["Left"], dualStickSdi=1), percent=60)
    jumps_d, end_d = sdi_jumps(rows_d, hit_d)
    consec = any(jumps_d[i][0] == jumps_d[i - 1][0] + 1 for i in range(1, len(jumps_d)))
    no_down = all(dy >= -0.05 for _, _, dy in jumps_d)
    res.note(f"single-stick (hitlag {end_s - hit_s}): {len(jumps_s)} jumps {jumps_s}")
    res.note(f"dual-stick   (hitlag {end_d - hit_d}): {len(jumps_d)} jumps {jumps_d}")
    res.row("dual-stick shifts > single-stick in same hitlag window",
            f"> {len(jumps_s)} and consecutive",
            f"{len(jumps_d)} jumps (consecutive={consec})",
            len(jumps_d) > len(jumps_s) and len(jumps_s) >= 2 and consec)
    res.row("grounded horizontal dual-stick SDI avoids downward Y",
            "all dy >= -0.05",
            str([dy for _, _, dy in jumps_d]),
            no_down and len(jumps_d) > 0)

    # Final-hitlag ASDI Up preserved under Dual-Stick SDI.
    rows_up, hit_up = r.hit_run("far", FSMASH, 45,
                                on(sdiAmount=7, sdiDir=SDI["Left"], asdi=ASDI["Up"], dualStickSdi=1), percent=60)
    _, end_up = sdi_jumps(rows_up, hit_up)
    step_base = rows_d[end_d][CPU]["y"] - rows_d[end_d - 1][CPU]["y"]
    step_up = rows_up[end_up][CPU]["y"] - rows_up[end_up - 1][CPU]["y"]
    asdi_diff = step_up - step_base
    res.note(f"ASDI Up launch step y: base {step_base:+.2f}, up {step_up:+.2f}, diff {asdi_diff:+.2f}")
    res.row("final-hitlag ASDI Up preserved with Dual-Stick SDI",
            "diff ~ +3.0",
            f"{asdi_diff:+.2f}",
            abs(asdi_diff - 3.0) < 0.35)

    # Post-hitlag TDI (Inwards vs Outwards) preserved under Dual-Stick SDI
    # (SDI Right keeps Fox on Mario's right so "Inwards" stays the same side).
    rows_in, hit_in = r.hit_run("far", FSMASH, 45,
                                on(tdi=TDI["Inwards"], sdiAmount=7, sdiDir=SDI["Right"], dualStickSdi=1), percent=60)
    rows_out, hit_out = r.hit_run("far", FSMASH, 45,
                                  on(tdi=TDI["Outwards"], sdiAmount=7, sdiDir=SDI["Right"], dualStickSdi=1), percent=60)
    ln_in, ln_out = launch(rows_in, hit_in), launch(rows_out, hit_out)
    res.note(f"TDI under dual-stick: Inwards {ln_in['angle']:.1f} deg, Outwards {ln_out['angle']:.1f} deg")
    res.row("post-hitlag TDI preserved (Inwards > Outwards + 4 deg)",
            "Inwards > Outwards + 4",
            f"{ln_in['angle']:.1f} / {ln_out['angle']:.1f}",
            ln_in["angle"] > ln_out["angle"] + 4.0)
    r.load("far", on())
    return res


def o_own_hitlag(r):
    """Attacker hitlag vs victim hitlag: when the CPU lands an attack with DI,
    ASDI, SDI and a ground counter enabled, its own hitlag (soStopModuleImpl
    isDamage = 0) must not trigger DI/SDI/ASDI stick inputs, hit counting, or
    a counter action. When P1 hits the CPU with the same options (isDamage = 1),
    DI/SDI/ASDI must still run."""
    res = Result("Own Hitlag (Attacker vs Victim)")
    opts = on(tdi=TDI["Outwards"], asdi=ASDI["Down"], sdiAmount=3, sdiDir=SDI["Left"],
              counterGround=GROUND["Spotdodge"])

    # Case 1: CPU lands Up Smash (ground list index 8) on P1 from 'near'.
    r.load("near", opts, percent=0)
    en_cpu, en_p1 = r.g.entry(CPU), r.g.entry(P1)
    ai = r.g.inputs(en_cpu)[0]
    stop_cpu = r.g.modules(en_cpu["fighters"][en_cpu["active"]])["stop"]
    stop_p1 = r.g.modules(en_p1["fighters"][en_p1["active"]])["stop"]
    pos = len(r.emu.log_text())
    pads, flags_cpu, flags_p1 = [], [], []

    def each(i, rows):
        if i == 0:
            r.cue.op(CueBlock.CUE, lst=LIST_GROUND, index=8)  # Up Smash
        pads.append(r.g.pad(ai))
        flags_cpu.append(r.emu.u8(stop_cpu + 0x1C))
        flags_p1.append(r.emu.u8(stop_p1 + 0x1C))
    rows = r.record(80, players=(CPU, P1), each=each)
    log = r.emu.log_text()[pos:]
    hit_logs = re.findall(r"cpu: P2 hit [^\r\n]+", log)

    # pads[k + 1] is the input written on row k; flags_*[k + 1] is stop+0x1C after row k.
    atk_lag = [k for k in range(len(rows) - 1) if rows[k][CPU]["hitlag"] > 0]
    p1_lag = [k for k in range(len(rows) - 1) if rows[k][P1]["hitlag"] > 0]
    if not atk_lag or not p1_lag:
        raise CpuError(f"CPU Up Smash did not connect (CPU statuses {statuses(rows)[:8]}, "
                       f"P1 statuses {statuses(rows, P1)[:8]})")
    h0, h1 = atk_lag[0], atk_lag[-1]
    trace = []
    for k in range(max(0, h0 - 1), min(len(rows) - 1, h1 + 6)):
        p = pads[k + 1]
        fc, fp = flags_cpu[k + 1], flags_p1[k + 1]
        trace.append(f"r{k}:cpu(st={sname(rows[k][CPU]['status'])},hl={rows[k][CPU]['hitlag']},"
                     f"flg={fc:#04x},dmg={(fc >> 5) & 1},pad=({p['x']:+.2f},{p['y']:+.2f},{p['buttons']:#x})) "
                     f"p1(st={sname(rows[k][P1]['status'])},hl={rows[k][P1]['hitlag']},"
                     f"hs={rows[k][P1]['hitstun']},flg={fp:#04x},dmg={(fp >> 5) & 1})")
    for line in trace:
        res.note(line)
    res.note(f"hit logs during CPU attack: {hit_logs}")
    res.note(f"CPU statuses: {statuses(rows)}")

    cpu_dmg_bits = [(flags_cpu[k + 1] >> 5) & 1 for k in atk_lag]
    p1_dmg_bits = [(flags_p1[k + 1] >> 5) & 1 for k in p1_lag]
    window = range(h0, min(len(rows) - 1, h1 + 5))
    nonzero_sticks = [(k, round(pads[k + 1]["x"], 2), round(pads[k + 1]["y"], 2))
                      for k in window if abs(pads[k + 1]["x"]) > 0.05 or abs(pads[k + 1]["y"]) > 0.05]
    spurious_escape = first(rows, h0, lambda x: x[CPU]["status"] == STATUS["Escape"])
    res.row("attacker hitlag flags: CPU isDamage / P1 isDamage", "all 0 / all 1",
            f"{sorted(set(cpu_dmg_bits))} / {sorted(set(p1_dmg_bits))}",
            bool(cpu_dmg_bits) and set(cpu_dmg_bits) == {0} and bool(p1_dmg_bits) and set(p1_dmg_bits) == {1})
    res.row("CPU stick during and after own hitlag", "neutral (no DI/SDI/ASDI)",
            nonzero_sticks if nonzero_sticks else "all neutral",
            not nonzero_sticks)
    res.row("CPU hit logs and counter during own attack", "no hit log, no Escape",
            f"logs={len(hit_logs)}, Escape={spurious_escape}",
            len(hit_logs) == 0 and spurious_escape is None)

    # Case 2: P1 hits the CPU with the same options -> victim hitlag (isDamage = 1) still runs DI/SDI/ASDI.
    pos2 = len(r.emu.log_text())
    vpads, vflags = [], []
    r.load("far", opts, percent=60)
    en_cpu = r.g.entry(CPU)
    ai = r.g.inputs(en_cpu)[0]
    stop_cpu = r.g.modules(en_cpu["fighters"][en_cpu["active"]])["stop"]

    def each2(i, rows):
        if i == 0:
            r.emu.hold(0, buttons=["A"], stick=FSMASH)
        elif i == 5:
            r.emu.neutral(0)
        vpads.append(r.g.pad(ai))
        vflags.append(r.emu.u8(stop_cpu + 0x1C))
    vrows = r.record(40, players=(CPU, P1), each=each2)
    r.emu.neutral(0)
    vlog = r.emu.log_text()[pos2:]
    vhit_logs = re.findall(r"cpu: P2 hit [^\r\n]+", vlog)
    vlag = [k for k in range(len(vrows) - 1) if vrows[k][CPU]["hitlag"] > 0]
    if not vlag:
        raise CpuError("P1 FSmash did not hit the CPU in victim control case")
    vdmg = [(vflags[k + 1] >> 5) & 1 for k in vlag]
    vsticks = [(k, round(vpads[k + 1]["x"], 2), round(vpads[k + 1]["y"], 2))
               for k in range(vlag[0], min(len(vrows) - 1, vlag[-1] + 4))
               if abs(vpads[k + 1]["x"]) > 0.05 or abs(vpads[k + 1]["y"]) > 0.05]
    res.note(f"victim hitlag rows {vlag[0]}..{vlag[-1]}, isDamage={sorted(set(vdmg))}, sticks={vsticks}, logs={vhit_logs}")
    res.row("victim hitlag with same options", "isDamage=1, DI/SDI sticks written, hit logged",
            f"isDamage={sorted(set(vdmg))}, sticks={len(vsticks)}, logs={len(vhit_logs)}",
            set(vdmg) == {1} and len(vsticks) >= 3 and len(vhit_logs) >= 1)
    return res


def tech_run(r, options, frames=160, delay=0):
    rows, hit = r.hit_run("near", DSMASH, frames, options, percent=50, delay=delay)
    land = first(rows, hit + 1, lambda x: x[CPU]["status"] not in FLYING)
    if land is None:
        raise CpuError("the CPU never landed")
    s = rows[land][CPU]["status"]
    p1x = rows[land][P1]["x"]
    away = 1 if rows[land][CPU]["x"] > p1x else -1
    moved = (rows[min(land + 25, len(rows) - 1)][CPU]["x"] - rows[land][CPU]["x"]) * away
    return rows, hit, land, s, moved


def o_tech(r):
    res = Result("Tech Options")
    for name, want, sign_ in (("In Place", "Passive", 0), ("Away", "Passive_Fb", 1), ("Towards", "Passive_Fb", -1),
                              ("None", "Down", 0)):
        rows, hit, land, s, moved = tech_run(r, on(tech=TECH[name], getup=GETUP["Stand"]))
        ok = sname(s) == want and (sign_ == 0 or moved * sign_ > 10)
        res.row(f"Tech {name}: status on landing (x moved away from P1)", f"{want}"
                f"{', away' if sign_ > 0 else (', toward' if sign_ < 0 else '')}", f"{sname(s)}, {moved:+.1f}", ok)
    for name, want, sign_ in (("Stand", "Down_Stand", 0), ("Away", "Down_Stand_Fb", 1),
                              ("Towards", "Down_Stand_Fb", -1), ("Attack", "Down_Stand_Attack", 0)):
        rows, hit, land, s, _ = tech_run(r, on(tech=TECH["None"], getup=GETUP[name]), frames=200)
        g = first(rows, land, lambda x: x[CPU]["status"] in (0x51, 0x52, 0x53))
        moved = 0.0
        if g is not None:
            away = 1 if rows[g][CPU]["x"] > rows[g][P1]["x"] else -1
            moved = (rows[min(g + 25, len(rows) - 1)][CPU]["x"] - rows[g][CPU]["x"]) * away
        got = sname(rows[g][CPU]["status"]) if g is not None else None
        res.row(f"missed tech, Get Up {name}", want + (", away" if sign_ > 0 else (", toward" if sign_ < 0 else "")),
                f"{got}, {moved:+.1f}", got == want and (sign_ == 0 or moved * sign_ > 5))
    # Tech Lockout: where in the window (the landing frame and the 19
    # before) the CPU presses: the guard press read from its input object.
    en = r.g.entry(CPU)
    ai = r.g.inputs(en)[0]
    for mode in ("Earliest", "Latest"):
        held = []
        rows, hit = r.hit_run("near", DSMASH, 160, on(tech=TECH["In Place"],
                                                      techLockout={"Earliest": 0, "Latest": 1}[mode]),
                              percent=50, react=lambda i, h: held.append(r.g.pad(ai)["buttons"] & GUARD) or None)
        land = first(rows, hit + 1, lambda x: x[CPU]["status"] not in FLYING)
        # held[i] is the input written in the frame of row i - 1.
        press = next((i - 1 for i in range(max(hit, 1) + 1, len(held)) if held[i] and not held[i - 1]), None)
        lead = land - press if land is not None and press is not None else None
        res.row(f"Tech Lockout {mode}: guard press before the landing", "0..19, " +
                ("about 15" if mode == "Earliest" else "about 5"), f"{lead} ({sname(rows[land][CPU]['status'])})",
                lead is not None and 0 <= lead <= 19 and rows[land][CPU]["status"] == STATUS["Passive"])
    return res


def o_tech_invis(r, name=None, check_getup=True):
    """Tech Invisibility (CPU), Tech Invisibility Delay (default 0 and a
    non-zero delay) and Tech Sound through the lab menu's real controller
    path: techs (0xBE..0xC0) invisible on 1-indexed frames dist + delay + 1
    through 19, visible again by frame 20, Tech Sound on frame dist without
    the delay; get-ups (0xAF..0xB2, 0xB8..0xBB, 0xE6..0xE9) invisible on
    ProjectPunch's 0-indexed subaction frames 5 + delay + 1 through 19."""
    name = name or r.cpu_name
    dist = TECH_DIST[name]
    res = Result(f"Tech Invisibility ({name})")
    emu, lab = r.emu, LT.Lab(r.emu, [])
    lab.find_settings()
    r.load("near", on(tech=TECH["In Place"], getup=GETUP["Stand"]), percent=50)
    default_delay = lab.setting(SET_TECH_DELAY)
    res.row("default Tech Invisibility Delay", 0, default_delay, default_delay == 0)

    def run_once(delay, miss_tech=False, tech_kind="In Place", shot_name=None):
        opts = on(tech=TECH["None"] if miss_tech else TECH[tech_kind], getup=GETUP["Stand"], missTechWait=0)
        want_status = STATUS["Passive"] if tech_kind == "In Place" else STATUS["Passive_Fb"]
        r.load("near", opts, percent=50)
        emu.h.call("resume")
        lab.open_menu()
        lab.main_to(LT.MAIN_CPU)
        lab.tap(["A"])
        lab.down(CPU_TECH_ROW)
        lab.tap(["A"])
        lab.down(TECH_INVIS_ROW)
        lab.set_choice(SET_TECH_INVIS, 1)  # CPU
        lab.down(TECH_DELAY_ROW - TECH_INVIS_ROW)
        if delay > 0:
            lab.set_choice(SET_TECH_DELAY, delay)
        got_mode = lab.setting(SET_TECH_INVIS)
        got_delay = lab.setting(SET_TECH_DELAY)
        lab.down(TECH_SOUND_ROW - TECH_DELAY_ROW)
        lab.toggle_on(SET_CPU + CPU_FIELDS["techSound"])
        lab.back_to_main(2)
        lab.close_menu()
        pos = len(emu.log_text())
        sound_at = []
        sound_rx = re.compile(r"cpu: P2 tech sound at tech frame (\d+) \(handle (-?\d+)\)")
        seen_pos = [pos]

        def smash(i, rows):
            cur_log = emu.log_text()
            if len(cur_log) > seen_pos[0]:
                for f, h in sound_rx.findall(cur_log[seen_pos[0]:]):
                    # Logged during frame i - 1 (the frame that just stepped).
                    sound_at.append((i - 1, int(f), int(h)))
                seen_pos[0] = len(cur_log)
            if i == 0:
                emu.hold(0, buttons=["A"], stick=DSMASH)
            elif i == 5:
                emu.neutral(0)
        frames = 220 if miss_tech else 160
        rows = r.record(frames, players=(CPU, P1), each=smash, extras=True)
        emu.neutral(0)
        cur_log = emu.log_text()
        if len(cur_log) > seen_pos[0]:
            for f, h in sound_rx.findall(cur_log[seen_pos[0]:]):
                sound_at.append((len(rows) - 1, int(f), int(h)))
        hit = first(rows, 1, lambda x: x[CPU]["status"] in FLYING)
        if hit is None:
            raise CpuError(f"DSMASH did not hit {name}")
        land = first(rows, hit + 1, lambda x: x[CPU]["status"] not in FLYING)
        if not miss_tech:
            if land is None or rows[land][CPU]["status"] != want_status:
                st = sname(rows[land][CPU]["status"]) if land is not None else None
                raise CpuError(f"{name} did not tech {tech_kind} (land={land}, status={st})")
            tech_rows = []
            for k in range(land, min(len(rows), land + 26)):
                if rows[k][CPU]["status"] != want_status:
                    break
                x = rows[k]["x"]
                tech_rows.append((k - land + 1, x["motion"], x["sub"], x["vis"],
                                  x["shadow"], x["cursor"], x["name_cursor"], x["shadow_proj"]))
            sounds = [(k - land + 1, f, h) for k, f, h in sound_at]
            if shot_name and not emu.headless:
                target = land + dist + delay + 4
                r.load("near", opts, percent=50)
                emu.h.call("write_mem", addr=lab.settings + SET_TECH_INVIS, hex=f"01{delay:02x}")
                shot_rows = r.record(target + 1, players=(CPU, P1), each=smash, extras=True)
                emu.neutral(0)
                emu.h.call("pause")
                nav.hold_game_still(emu, True)
                emu.h.call("resume")
                time.sleep(0.25)
                path, w, h = emu.screenshot(shot_name)
                emu.h.call("pause")
                nav.hold_game_still(emu, False)
                emu.h.call("resume")
                sx = shot_rows[-1]["x"]
                res.note(f"screenshot {shot_name}: {path} ({w}x{h}) at mframe={sx['motion']:.1f} "
                         f"vis={sx['vis']} sh={sx['shadow']} proj={sx['shadow_proj']} "
                         f"cur={sx['cursor']} ncur={sx['name_cursor']}")
            return got_mode, got_delay, tech_rows, sounds
        if land is None:
            raise CpuError(f"{name} never landed for getup")
        g = first(rows, land, lambda x: x["x"]["sub"] in GETUP_SUBS)
        if g is None:
            raise CpuError(f"{name} never entered getup subaction after missed tech")
        getup_rows = []
        for k in range(g, min(len(rows), g + 30)):
            x = rows[k]["x"]
            if x["sub"] not in GETUP_SUBS:
                break
            getup_rows.append((k - g, x["motion"], x["sub"], x["vis"],
                               x["shadow"], x["cursor"], x["name_cursor"], x["shadow_proj"]))
        return got_mode, got_delay, getup_rows, sound_at

    def check_hide_triples(label, seq, want_hidden_keys, shown_key, key_fn):
        by_key = {key_fn(row): row for row in seq}
        want_set = set(want_hidden_keys)
        pre_key = want_hidden_keys[0] - 1
        pre = by_key.get(pre_key)
        pre_cur = pre[5] if pre else 0
        proj_off = [k for k in want_hidden_keys if k in by_key and by_key[k][7] == 0]
        cur_bad = [(key_fn(row), row[5], row[6]) for row in seq if key_fn(row) in want_set and (row[5] != 0 or row[6] != 0)]
        shown = by_key.get(shown_key)
        shown_triple = (shown[7], shown[5], shown[6]) if shown else None
        res.row(f"{label}: shadow projection off on hidden frames", f"{want_hidden_keys[0]}..{want_hidden_keys[-1]}",
                f"{proj_off[0]}..{proj_off[-1]} ({len(proj_off)}f)" if proj_off else "none", proj_off == want_hidden_keys)
        res.row(f"{label}: cursor/name cursor off on hidden frames", "all (0, 0)",
                "all (0, 0)" if not cur_bad else f"{len(cur_bad)} bad, e.g. {cur_bad[:3]}", not cur_bad)
        res.row(f"{label}: shadow proj/cursors back on first shown frame", (1, pre_cur, 1), shown_triple,
                shown is not None and pre is not None and pre[7] == 1 and pre[6] == 1
                and shown[7] == 1 and shown[5] == pre_cur and shown[6] == 1)

    tech_runs = [(0, "In Place", f"tech-invis-{name}-inplace" if check_getup else None),
                 (3, "In Place", None)]
    if check_getup:
        tech_runs.append((0, "Away", f"tech-invis-{name}-away"))
    for delay, tech_kind, shot_name in tech_runs:
        got_mode, got_delay, tech_rows, sounds = run_once(delay, tech_kind=tech_kind, shot_name=shot_name)
        tag = f"delay {delay}" if tech_kind == "In Place" else f"away delay {delay}"
        one_to_one = all(abs(m - (sf - 1)) < 0.01 for sf, m, *_ in tech_rows[:21])
        hidden = [sf for sf, _, _, vis, *_ in tech_rows if vis == 0]
        want_hidden = list(range(dist + delay + 1, 21))
        vis21 = next((vis for sf, _, _, vis, *_ in tech_rows if sf == 21), None)
        res.note(f"{tag} (setting mode={got_mode}, delay={got_delay}): frames 1..21 (sf, mframe, sub, vis, sh, cur, ncur, proj)="
                 f"{[(sf, round(m, 1), hex(sub), v, sh, c, nc, pr) for sf, m, sub, v, sh, c, nc, pr in tech_rows[:22]]}; sounds={sounds}")
        res.row(f"{tag}: 1 anim frame per game frame", "mframe == sf - 1 (frames 1..21)",
                f"{len(tech_rows)} frames, exact={one_to_one}", len(tech_rows) >= 21 and one_to_one)
        res.row(f"{tag}: hidden frames (1-indexed)", f"{want_hidden[0]}..{want_hidden[-1]}",
                f"{hidden[0]}..{hidden[-1]} ({len(hidden)}f)" if hidden else "none", hidden == want_hidden)
        res.row(f"{tag}: visible again on frame 21", 1, vis21, vis21 == 1)
        check_hide_triples(tag, tech_rows, want_hidden, 21, lambda row: row[0])
        res.row(f"{tag}: Tech Sound on frame {dist}", f"[(step {dist}, log {dist}, handle >= 0)]",
                sounds, len(sounds) == 1 and sounds[0][0] == dist and sounds[0][1] == dist and sounds[0][2] >= 0)
    if check_getup:
        for delay in (0, 3):
            got_mode, got_delay, getup_rows, sounds = run_once(delay, miss_tech=True)
            hidden_mf = [int(m) for _, m, _, vis, *_ in getup_rows if vis == 0]
            want_mf = list(range(6 + delay, 20))
            vis20 = next((vis for _, m, _, vis, *_ in getup_rows if int(m) == 20), None)
            sub = hex(getup_rows[0][2]) if getup_rows else "none"
            res.note(f"getup delay {delay} (sub {sub}): frames (idx, mframe, sub, vis, sh, cur, ncur, proj)="
                     f"{[(idx, round(m, 1), hex(s), v, sh, c, nc, pr) for idx, m, s, v, sh, c, nc, pr in getup_rows[:22]]}; sounds={sounds}")
            res.row(f"getup delay {delay} ({sub}): hidden 0-indexed mframes", f"{want_mf[0]}..{want_mf[-1]}",
                    f"{hidden_mf[0]}..{hidden_mf[-1]} ({len(hidden_mf)}f)" if hidden_mf else "none",
                    hidden_mf == want_mf)
            res.row(f"getup delay {delay}: visible again on mframe 20", 1, vis20, vis20 == 1)
            check_hide_triples(f"getup delay {delay}", getup_rows, want_mf, 20, lambda row: int(row[1]))
            res.row(f"getup delay {delay}: no Tech Sound", "[]", sounds, len(sounds) == 0)
    # Restore Tech Options defaults.
    emu.h.call("write_mem", addr=lab.settings + SET_TECH_INVIS, hex="0000")
    r.opt.set(control=TRAINING, techSound=0)
    return res


def o_tech_invis_sheik(r):
    """Transform Zelda (ftKind 0x0D, dist 3) -> Sheik (ftKind 0x0E, dist 4)
    mid-match, then verify Tech Invisibility Delay and Tech Sound use Sheik's
    table entry (4) rather than Zelda's (3)."""
    emu = r.emu
    r.refresh()
    en0 = r.g.entry(CPU)
    kind0 = en0["kinds"][en0["active"]]
    r.drv.play(CPU, [NEUTRAL_STEP], loop=True)
    set_training_option(emu, "Attack")
    emu.h.call("pause")
    emu.step(frames=20)
    # Down+B transforms Zelda into Sheik. Wait for #fighter change end (~15
    # frames after m_kind flips) and clear cached module pointers before
    # reading Sheik's Fighter*.
    r.drv.play(CPU, [(B, 0.0, -1.0, 6), (0, 0.0, 0.0, 240)], ai_off=True)
    step_until(emu, "Zelda to transform to Sheik",
               lambda: r.g.entry(CPU)["kinds"][r.g.entry(CPU)["active"]] == FT_SHEIK, 240)
    emu.step(frames=20)
    r.g.mods.clear()
    r.drv.play(CPU, [NEUTRAL_STEP], loop=True)
    step_until(emu, "Sheik to finish transforming",
               lambda: r.g.fighter(r.g.entry(CPU))["status"] == STATUS["Wait"], 240)
    emu.step(frames=15)
    r.refresh()
    en1 = r.g.entry(CPU)
    kind1 = en1["kinds"][en1["active"]]
    r.states.save(emu, "start")
    save_hit_state(r, "near", 8.5)
    emu.h.call("resume")
    res = o_tech_invis(r, name="sheik", check_getup=False)
    res.name = "Tech Invisibility (Zelda -> Sheik)"
    res.row("CPU transformed Zelda -> Sheik", f"{hex(FT_ZELDA)} -> {hex(FT_SHEIK)}",
            f"{hex(kind0)} -> {hex(kind1)}", kind0 == FT_ZELDA and kind1 == FT_SHEIK)
    return res


def o_tech_random(r, trials=30):
    res = Result("Tech Random")
    counts, wrong = {}, []
    for t in range(trials):
        # A different wait before the hit each time: the CPU's choices are
        # stirred with the frame counter, which the savestate otherwise replays.
        pos = len(r.emu.log_text())
        rows, hit, land, s, moved = tech_run(r, on(tech=TECH["Random"], techChance=[50, 0, 0, 50]), frames=100,
                                             delay=t)
        counts[sname(s)] = counts.get(sname(s), 0) + 1
        chose = re.findall(r"cpu: P2 tech choice (\d)", r.emu.log_text()[pos:])
        want = {"1": "Passive", "4": "Down"}.get(chose[-1] if chose else "", "?")
        if want != sname(s):
            wrong.append((t, chose, sname(s)))
    res.row("each trial did what was chosen (log choice vs landing status)", "all", f"{trials - len(wrong)}"
            f" of {trials}{', ' + str(wrong[:3]) if wrong else ''}", not wrong)
    res.note(f"{trials} trials with chances in place 50, away 0, toward 0, miss 50: {counts}")
    res.row(f"Random over {trials}: Passive / Down", "about 15 / 15, both >= 6, nothing else",
            f"{counts.get('Passive', 0)} / {counts.get('Down', 0)}",
            set(counts) <= {"Passive", "Down"} and counts.get("Passive", 0) >= 6 and counts.get("Down", 0) >= 6)
    return res


def o_silence(r):
    res = Result("silence (options)")
    out = {}
    for label, opts in (("options on", on()), ("options off", None)):
        r.load("start")
        r.drv.play(CPU, WALK_OFF)
        if opts:
            r.opt.set(**opts)
        flags = []

        def each(i, rows):
            if i > 50:
                flags.append(r.g.ai_flag(1))
        rows = r.record(46 + 400, each=each)
        seen = {sname(x[CPU]["status"]) for x in rows}
        ko = any(x[CPU]["status"] in GONE for x in rows)
        tried = any(x[CPU]["status"] == STATUS["Jump_Aerial"] or x[CPU]["status"] >= 0x112
                    for x in rows if x[CPU]["status"] not in GONE)
        out[label] = (ko, seen, sorted(set(flags)), tried)
        res.note(f"{label}: KO'd {ko}; statuses {sorted(seen)}; AI flag values {sorted(set(flags))}")
    res.row("options on: off-stage CPU", "falls, never jumps or specials; AI flag 0",
            f"KO {out['options on'][0]}, flags {out['options on'][2][:3]}",
            out["options on"][0] and not out["options on"][1] & {"Jump_Aerial", "Fall_Special"})
    res.row("options off: the AI is back", "tries to recover; AI flag 1",
            f"tries {out['options off'][3]} (KO'd {out['options off'][0]}), flags {out['options off'][2]}",
            out["options off"][3] and out["options off"][2] == [1])
    res.row("options on: no recovery attempt", "no", "yes" if out["options on"][3] else "no",
            not out["options on"][3])
    return res


FD_EDGE = 86.0
TUMBLE = set(range(STATUS["Damage_Fly"], STATUS["Damage_Fall"] + 1))
CLIFF = set(range(0x73, 0x7C))
FACING = 0x40  # posture module: the facing float


def o_throw_behavior(r):
    """Behavior Shield after a throw out of shield (a tester's report): the
    shielding CPU is grabbed and forward-thrown by Mario, at 0% (it lands)
    and at 300% (it is KO'd and respawns), with and without the air counter
    action Wiggle Fastfall, Recovery Off and (the default) Game AI. It must
    shield again once back on the ground, Behavior Shield on throughout."""
    res = Result("Behavior after a throw")
    for rec, pct, label in [(rec, pct, label) for rec in (RECOVERY["Off"], RECOVERY["Game"]) for pct in (0, 300)
                            for label in ("None", "Wiggle Fastfall")]:
        r.load("near", on(behavior=BEHAVIOR["Shield"], recovery=rec, counterAir=AIR_ACTIONS[label]),
               percent=pct)
        seen = {}

        def each(i, rows):
            if i == 30:
                r.emu.hold(0, buttons=["Z"])
            elif i == 34:
                r.emu.neutral(0)
            c = rows[-1][CPU] if rows else None
            if c is not None and "held" not in seen and c["status"] in (0x3D, 0x3E):
                seen["held"] = i
                toward = 255 if rows[-1][CPU]["x"] > rows[-1][P1]["x"] else 0
                r.emu.hold(0, stick=(toward, 128))
            elif "held" in seen and i == seen["held"] + 3:
                r.emu.neutral(0)
        rows = r.record(1200 if pct else 400, players=(CPU, P1), each=each)
        thrown = first(rows, 0, lambda x: x[CPU]["status"] == 0x42)
        ko = first(rows, thrown or 0, lambda x: x[CPU]["status"] in GONE) if pct else None
        start = ko if ko is not None else thrown
        back = first(rows, start or 0, lambda x: not x[CPU]["air"] and x[CPU]["status"] not in GONE | FLYING) \
            if start is not None else None
        guard = first(rows, back, lambda x: x[CPU]["status"] in (STATUS["Guard_On"], STATUS["Guard"])) \
            if back is not None else None
        res.note(f"Recovery {rec}, {pct}%, {label}: thrown row {thrown}, KO row {ko}, grounded row {back}, "
                 f"shield row {guard}; statuses {statuses(rows)[:30]}")
        res.row(f"Recovery {rec}, {pct}%, counter (air) {label}: shield again after the throw"
                + (" and the KO" if pct else ""),
                "thrown" + (", KO'd" if pct else "") + ", then Guard within 30 rows of landing",
                f"thrown {thrown}, KO {ko}, grounded {back}, shield {guard}",
                thrown is not None and (ko is not None or not pct) and guard is not None and guard <= back + 30)
    return res


def o_counter_tumble(r):
    """Behavior Shield after an air counter action in tumble, no KO: Mario's
    up smash from 'near' at the lowest percent from 60 whose hitstun ends in
    the air in tumble, so the action runs; then the CPU lands on the stage
    and must shield again (after its tech or get-up), Recovery Off and Game
    AI, with Tumble Fastfall and Wiggle Fastfall."""
    res = Result("Behavior after a counter action in tumble")
    pct = None
    for p in (60, 80, 100, 120):
        rows, hit = r.hit_run("near", USMASH, 200, on(behavior=BEHAVIOR["Stand"]), percent=p)
        e = first(rows, hit + 1, lambda x: x[CPU]["hitstun"] == 0 and x[CPU]["hitlag"] == 0)
        if e is not None and rows[e][CPU]["air"] and rows[e][CPU]["status"] in TUMBLE \
                and first(rows, hit, lambda x: x[CPU]["status"] in GONE) is None:
            pct = p
            break
    if pct is None:
        raise CpuError("no percent from 60 to 120 leaves the CPU in the air in tumble when its hitstun ends")
    res.note(f"percent {pct}")
    for rec in (RECOVERY["Off"], RECOVERY["Game"]):
        for label in ("Tumble Fastfall", "Wiggle Fastfall"):
            def react(i, h):
                if h is not None and i == h + 1:
                    r.opt.set(**on(behavior=BEHAVIOR["Shield"], recovery=rec, counterAir=AIR_ACTIONS[label]))
                return None
            rows, hit = r.hit_run("near", USMASH, 600, on(behavior=BEHAVIOR["Stand"], recovery=rec,
                                                          counterAir=AIR_ACTIONS[label]), percent=pct, react=react)
            ko = first(rows, hit, lambda x: x[CPU]["status"] in GONE)
            land = first(rows, hit + 1, lambda x: not x[CPU]["air"] and x[CPU]["status"] not in FLYING)
            guard = first(rows, land, lambda x: x[CPU]["status"] in (STATUS["Guard_On"], STATUS["Guard"])) \
                if land is not None else None
            res.note(f"Recovery {rec}, {label}: landed row {land}, shield row {guard}, KO row {ko}; "
                     f"statuses from the hit {statuses(rows[hit:])[:24]}")
            res.row(f"Recovery {rec}, counter (air) {label}: shield again after landing",
                    "not KO'd, Guard within 90 rows of landing", f"KO {ko}, landed {land}, shield {guard}",
                    ko is None and land is not None and guard is not None and guard <= land + 90)
    return res


def o_after_tech_counter(r):
    """After an air counter in tumble finishes (state kAfter, e.g. Tumble
    Fastfall), both a missed tech (Down -> get up) and a successful tech
    (Passive) must run the ground counter action (
    2284-2289)."""
    res = Result("Ground counter after kAfter tech")
    for label, tech_opt, land_want in (("Tech None (missed)", TECH["None"], "Down"),
                                       ("Tech In Place", TECH["In Place"], "Passive")):
        opts = on(counterAir=AIR_ACTIONS["Tumble Fastfall"], counterGround=8,
                  tech=tech_opt, getup=GETUP["Stand"], recovery=RECOVERY["Off"])
        rows, hit = r.hit_run("near", USMASH, 220, opts, percent=80)
        land = first(rows, hit + 1, lambda x: not x[CPU]["air"] and x[CPU]["status"] not in FLYING)
        st_land = sname(rows[land][CPU]["status"]) if land is not None else None
        usmash = first(rows, land, lambda x: x[CPU]["status"] == STATUS["Attack_Hi4_Start"]) \
            if land is not None else None
        res.note(f"{label}: hit {hit}, land {land} ({st_land}), Up Smash {usmash}; "
                 f"statuses {statuses(rows[land or hit:])[:10]}")
        res.row(f"{label} after Tumble Fastfall: lands {land_want}, then ground counter Up Smash",
                f"{land_want}, Attack_Hi4_Start", f"{st_land}, {usmash}",
                land is not None and st_land == land_want and usmash is not None)
    return res


def put_fighter(r, en, x, y):
    """Writes a fighter's position (and its previous one)."""
    post = r.g.modules(en["fighters"][en["active"]])["posture"]
    pos = struct.pack(">ff", x, y).hex()
    r.emu.h.call("write_mem", addr=post + 0xC, hex=pos)
    r.emu.h.call("write_mem", addr=post + 0x18, hex=pos)


def standing(r, en):
    f = r.g.fighter(en)
    return not f["air"] and f["status"] == STATUS["Wait"]


def land_at(r, en, x, y):
    """Mario hops, is put at (x, y) while in the air and lands on what is
    under it; returns his state standing there. (Put below a pass-through
    platform he was above, he lands on it: the game sees him cross it. The
    way down is drop_through.)"""
    emu = r.emu
    emu.h.call("resume")
    r.drv.play(P1, [(JUMP, 0.0, 0.0, 1), (0, 0.0, 0.0, 200)])
    emu.advance(12)
    emu.h.call("pause")
    put_fighter(r, en, x, y)
    emu.h.call("resume")
    emu.advance(20)
    emu.wait_for("Mario to land", lambda: standing(r, en), 400)
    emu.advance(10)
    r.drv.stop()
    return r.g.fighter(en)


def drop_through(r, en):
    """Mario, standing on a pass-through platform, taps down, falls through
    it and lands on what is below; returns his state standing there."""
    emu = r.emu
    y0 = r.g.fighter(en)["y"]
    emu.h.call("resume")
    r.drv.play(P1, [(0, 0.0, -1.0, 6), (0, 0.0, 0.0, 200)])
    emu.wait_for("Mario to drop through the platform", lambda: r.g.fighter(en)["y"] < y0 - 3.0, 120)
    emu.wait_for("Mario to land", lambda: standing(r, en), 400)
    emu.advance(10)
    r.drv.stop()
    return r.g.fighter(en)


def place_cpu(r, frames=40, cpu_script=None):
    """D-pad down held from the third frame on (the lab's Place CPU, its
    default binding, placed after 10 frames of hold); one row per frame."""
    emu = r.emu
    if cpu_script:
        r.drv.play(CPU, cpu_script, ai_off=True)

    def each(i, rows):
        if i == 2:
            emu.hold(0, ["DDOWN"])
        elif i == 20:
            emu.neutral(0)
    rows = r.record(frames, players=(CPU, P1), each=each)
    emu.neutral(0)
    r.drv.stop()
    return rows


def o_place_platforms(r):
    """On Battlefield: a fighter placed standing exactly at a pass-through
    platform's height by Recording's lite start-state restore (start_state.cpp
    place(): warp + attachGround + setStGroundForce) must stay on it without
    falling through. Mario is saved on each of the three Battlefield platforms
    and on the main floor (with 3 Training CPUs so Recording uses the lite
    start state), moved off that surface, and restored: he must stand at the
    saved spot in Wait for the rest of the run, including when restored out of
    a jump."""
    res = Result("placing on platforms")
    emu = r.emu
    r.drv.stop()
    r.opt.set(control=TRAINING)
    found = re.findall(r"\] record: test block at ([0-9a-f]{8})", emu.log_text())
    if not found:
        raise CpuError("place-platforms: no record test block in log")
    rec_tb = int(found[-1], 16)
    REQ_SAVE_OR_RESTORE, REQ_RESTORE, REQ_DELETE, REQ_RESAVE = 1, 2, 3, 4

    def rec_request(req):
        emu.h.call("write_mem", addr=rec_tb + 4, hex=f"{req:08x}")
        emu.wait_for(f"record request {req}", lambda: emu.u32(rec_tb + 4) == 0, 120, every=1)

    en, p1 = r.g.entry(CPU), r.g.entry(P1)
    emu.advance(60)
    m0, c0 = r.g.fighter(p1), r.g.fighter(en)
    lr = m0["lr"]
    top = land_at(r, p1, 0.0, m0["y"] + 60.0)
    floor = drop_through(r, p1)
    res.note(f"Mario starts at ({m0['x']:.2f}, {m0['y']:.2f}) facing {lr:+.0f}, the CPU at ({c0['x']:.2f}, "
             f"{c0['y']:.2f}); dropped at x 0 from above he lands at y {top['y']:.2f}, and through "
             f"that platform at y {floor['y']:.2f}")
    res.row("three platforms above the main floor", "two side platforms, a higher one, the floor below",
            f"y {m0['y']:.2f}, {c0['y']:.2f}, {top['y']:.2f}; floor {floor['y']:.2f}",
            abs(m0["y"] - c0["y"]) < 0.05 and top["y"] > m0["y"] + 5 and floor["y"] < m0["y"] - 5)

    # Raise Training CPUs to 3 so Recording falls back to LiteStartState
    # (start_state.cpp place(): warp + attachGround + setStGroundForce).
    nav.set_training_cpus(emu, 3)
    emu.wait_for("4 fighters for lite start state", lambda: len(LT.Fighters(emu).entries()) == 4, 300, every=2)
    emu.advance(30)
    r.refresh()
    p1 = r.g.entry(P1)

    spots = [("left side platform", m0["x"], m0["y"]), ("right side platform", c0["x"], c0["y"]),
             ("top platform", 0.0, top["y"]), ("main floor", -55.0, floor["y"])]
    try:
        for idx, (name, xc, ys) in enumerate(spots):
            if name == "main floor":
                # Drop through top platform, then land at x = -55 on the main floor clear of the 3 CPUs.
                drop_through(r, p1)
                m = land_at(r, p1, xc, ys + 8.0)
            else:
                m = land_at(r, p1, xc, ys + 8.0)
            sx = m["x"]
            pos = len(emu.log_text())
            rec_request(REQ_SAVE_OR_RESTORE if idx == 0 else REQ_RESAVE)
            emu.advance(4)
            lite_saved = bool(re.search(r"record: positions saved(?: again)? \(lite", emu.log_text()[pos:]))
            # Move Mario off the saved surface before restoring.
            if name == "main floor":
                emu.h.call("resume")
                r.drv.play(P1, [(0, 1.0, 0.0, 18), (0, 0.0, 0.0, 30)])
                emu.advance(35)
                r.drv.stop()
            else:
                drop_through(r, p1)
            # Trigger lite restore and record 30 frames to ensure Mario stays on the surface.
            def do_restore(i, rows):
                if i == 2:
                    emu.h.call("write_mem", addr=rec_tb + 4, hex=f"{REQ_RESTORE:08x}")
            rows = r.record(30, players=(P1, CPU), each=do_restore)
            k = first(rows, 2, lambda x: abs(x[P1]["x"] - sx) < 0.05 and abs(x[P1]["y"] - ys) < 0.2)
            after = rows[k:] if k is not None else []
            bad = [x["i"] for x in after if x[P1]["air"] or x[P1]["status"] != STATUS["Wait"]
                   or abs(x[P1]["y"] - ys) > 0.05 or abs(x[P1]["x"] - sx) > 0.05]
            end = rows[-1][P1]
            res.row(f"{name}: lite restore at ({sx:.1f}, {ys:.2f})", f"lite saved, standing from row {k} on",
                    f"lite={lite_saved}, {len(after) - len(bad)} of {len(after)} rows; first off row {bad[:1]}; "
                    f"at end ({end['x']:.1f}, {end['y']:.2f}) {sname(end['status'])}"
                    f"{' in the air' if end['air'] else ''}",
                    lite_saved and k is not None and len(after) >= 15 and not bad and abs(m["y"] - ys) < 0.05)

        # Main floor: Mario jumps into the air, then lite restore snaps him back grounded at once.
        emu.h.call("resume")
        emu.advance(10)
        r.drv.play(P1, [(JUMP, 0.0, 0.0, 3), (0, 0.0, 0.0, 60)])

        def do_air_restore(i, rows):
            if i == 10:
                emu.h.call("write_mem", addr=rec_tb + 4, hex=f"{REQ_RESTORE:08x}")
        rows = r.record(36, players=(P1, CPU), each=do_air_restore)
        r.drv.stop()
        up = first(rows, 0, lambda x: x[P1]["air"])
        k = first(rows, 10, lambda x: not x[P1]["air"] and abs(x[P1]["y"] - floor["y"]) < 0.05)
        after = rows[k:k + 15] if k is not None else []
        bad = [x["i"] for x in after if x[P1]["air"] or x[P1]["status"] != STATUS["Wait"]
               or abs(x[P1]["y"] - floor["y"]) > 0.05]
        res.row("main floor, Mario restored out of a jump", "airborne, then standing at once (by row 20)",
                f"airborne from row {up}, on ground on row {k} ({sname(rows[k][P1]['status']) if k else None}), "
                f"first off row {bad[:1]}",
                up is not None and up < 10 and k is not None and k <= 20 and len(after) == 15 and not bad)
    finally:
        emu.h.call("resume")
        rec_request(REQ_DELETE)
        nav.set_training_cpus(emu, 1)
        emu.advance(20)
    return res


def o_intangible(r):
    """Intangibility behaviour: the hit connects (the
    attacker gets hitlag) but the CPU does not react, stays at 0% whatever
    its locked percent, and a grab misses it; turned off, a grab holds it.
    The CPU's counter action and SDI do not start on such a hit (the lab
    looks for hitstun). After the jab the two fighters move apart 0.3 a
    frame for two frames whether the hit connects or passes through
    (measured, Mario and Fox on Final Destination); the lab nudge-disable,
    which would keep the CPU still, is not built."""
    res = Result("Intangibility")
    capture = (0x3D, 0x3E)  # Capture_Pulled, Capture_Wait
    runs = (("forward smash", "far", FSMASH, 5, 0.01, {}), ("jab", "near", JAB, 3, 0.7, {}),
            ("jab, Up Smash counter and SDI 3 set", "near", JAB, 3, 0.7,
             {"counterGround": GROUND["Up Smash"], "sdiAmount": 3}))
    for name, state, stick, hold, most, extra in runs:
        r.load(state, on(intangible=1, **extra), percent=60)

        def each(i, rows):
            if i == 0:
                r.emu.hold(0, buttons=["A"], stick=stick)
            elif i == hold:
                r.emu.neutral(0)
        rows = r.record(60, players=(CPU, P1), each=each, extras=True)
        lag = max(x[P1]["hitlag"] for x in rows)
        hit = any(x[CPU]["hitstun"] > 0 or x[CPU]["status"] in HIT for x in rows)
        kb = any(x["x"]["kb"] not in (None, (0.0, 0.0)) for x in rows)
        # Row 0 finishes the saved frame, before the options are applied.
        dmg = {round(x[CPU]["damage"], 1) for x in rows[2:]}
        moved = max(abs(x[CPU]["x"] - rows[0][CPU]["x"]) + abs(x[CPU]["y"] - rows[0][CPU]["y"]) for x in rows)
        sts = sorted({sname(x[CPU]["status"]) for x in rows})
        res.row(f"{name} on the CPU", f"attacker hitlag; no hitstun or knockback; 0%; Wait; moved <= {most}",
                f"P1 hitlag up to {lag}, hit {hit}, knockback {kb}, damage {sorted(dmg)}, moved {moved:.2f}, CPU {sts}",
                lag > 0 and not hit and not kb and dmg == {0.0} and sts == ["Wait"] and moved <= most)

    r.load("near", on(intangible=1), percent=0)

    def grab(i, rows):
        if i == 2:
            r.emu.hold(0, buttons=["Z"])
        elif i == 6:
            r.emu.neutral(0)
        elif i == 60:
            r.opt.set(intangible=0)
        elif i == 62:
            r.emu.hold(0, buttons=["Z"])
        elif i == 66:
            r.emu.neutral(0)
    rows = r.record(110, players=(CPU, P1), each=grab)
    before = [x["i"] for x in rows[:60] if x[CPU]["status"] in capture or x[P1]["status"] == STATUS["Catch_Pull"]]
    after = first(rows, 60, lambda x: x[CPU]["status"] in capture)
    res.row("grab on the CPU, then again with the option off", "misses, then holds",
            f"held rows {before[:3]}, then held from row {after}", not before and after is not None)
    return res


def o_percent_lock(r):
    """Lab > CPU Percent locked while CPU Control is Lab: the CPU, hit,
    keeps its percent (the lock finds the CPU by the match setup, not by its
    AI flag, which the driver clears). Unlocked, the same hit adds damage."""
    res = Result("CPU Percent lock under Lab control")
    got = {}
    for lock in (True, False):
        r.load("near", on(), percent=40)
        r.opt.cpu_percent(40, lock)
        ai = []

        def each(i, rows):
            ai.append(r.g.ai_flag(1))
            if i == 0:
                r.emu.hold(0, buttons=["A"])
            elif i == 3:
                r.emu.neutral(0)
        rows = r.record(40, players=(CPU, P1), each=each)
        r.emu.neutral(0)
        hit = any(b[CPU]["hitlag"] > a[CPU]["hitlag"] for a, b in zip(rows, rows[1:]))
        flags = set(ai[2:])
        got[lock] = (hit, round(rows[0][CPU]["damage"], 1), round(rows[-1][CPU]["damage"], 1), flags)
        res.note(f"locked {lock}: jab hit {hit}, damage {got[lock][1]} -> {got[lock][2]}, AI flag {sorted(flags)}")
    res.row("locked at 40%, CPU Control Lab: jabbed, percent", "40 -> 40 (hit, AI flag cleared)",
            f"{got[True][1]} -> {got[True][2]} (hit {got[True][0]}, AI flag {sorted(got[True][3])})",
            got[True][0] and got[True][1] == got[True][2] == 40.0 and got[True][3] == {0})
    res.row("unlocked: the same jab adds damage", "rises", f"{got[False][1]} -> {got[False][2]}",
            got[False][0] and got[False][2] > got[False][1])
    return res


def o_finish(r):
    """Finish with control active, then start the next match: every slot
    released at the end and the module back at its baseline."""
    res = Result("match end with control on")
    emu = r.emu
    pos = len(emu.log_text())
    r.load("start", on(behavior=BEHAVIOR["Shield"]))
    emu.h.call("resume")
    emu.advance(30)
    held = r.g.fighter(r.g.entry(CPU))["status"]
    nav.finish_training(emu)
    emu.advance(60)
    log = emu.log_text()[pos:]
    rel = re.findall(r"cpu: release slot (\d) after \d+ frames \(match ending\)", log)
    back = re.findall(r"cpu: slot (\d) AI flag back to (\d+) \(match ending\)", log)
    summary = re.search(r"cpu: (\d+) takes, (\d+) releases, (\d+) AI flags put back", log)
    unload = re.search(L.rx(L.UNLOADED).pattern, log)
    res.row("before Finish the CPU shields", "Guard", sname(held), held == STATUS["Guard"])
    res.row("at the match end", "slot 1 released, flag back, unload at baseline",
            f"released {rel}, flags {back}, {summary.group(0) if summary else None}, "
            f"baseline {unload.group(4) if unload else None}",
            "1" in rel and ("1", "1") in back and unload is not None and unload.group(4) == "yes"
            and summary is not None and summary.group(1) == summary.group(2))
    return res


def o_multi(r):
    """Three CPUs (Training's No. of CPUs copies the first) under the options."""
    res = Result("3 CPUs")
    emu = r.emu
    # The harness script would hold slot 1 (prepare leaves it playing).
    r.drv.stop()
    nav.set_training_cpus(emu, 3)
    emu.advance(30)
    r.refresh()
    r.opt.set(**on(behavior=BEHAVIOR["Shield"], infiniteShields=SHIELDS["On"], shieldHealth=50))
    # The extra CPUs load from the disc in real time, so the number of game
    # frames until they exist and shield depends on how busy the machine is:
    # wait for the state instead of a fixed count (30 s of game time at most).
    cpus, rows = [], []
    for _ in range(60):
        emu.advance(30)
        r.refresh()
        cpus = [en for en in r.g.entries() if en["player"] != P1]
        rows = []
        try:
            for en in cpus:
                f = r.g.fighter(en)
                rows.append((en["player"], en["slot"], sname(f["status"]), r.g.ai_flag(en["slot"])))
        except HarnessError:
            # An entry exists a few frames before its fighter does; reading
            # through its empty pointer fails. Not ready yet.
            rows = []
            continue
        if len(cpus) == 3 and all(st == "Guard" and fl == 0 for _p, _s, st, fl in rows):
            break
    res.note(f"(player, slot, status, AI flag): {rows}")
    res.row("every CPU: slot == player, Guard, AI flag 0", f"{len(cpus)} x (p, p, Guard, 0)", rows,
            len(cpus) == 3 and all(p == s and st == "Guard" and fl == 0 for p, s, st, fl in rows))
    r.opt.set(control=TRAINING)
    emu.advance(30)
    flags = [r.g.ai_flag(en["slot"]) for en in cpus]
    res.row("options off: AI flags back", "1 1 1", flags, flags == [1, 1, 1])
    nav.set_training_cpus(emu, 1)
    return res


def o_nana(r):
    """An Ice Climbers CPU: Nana is driven in step with Popo."""
    res = Result("Ice Climbers")
    emu = r.emu
    en = r.g.entry(CPU)
    main, sub = r.g.inputs(en)
    if sub is None:
        res.check("the CPU is Ice Climbers", False)
        return res
    r.drv.play(CPU, [NEUTRAL_STEP], loop=True)
    emu.advance(10)
    for also in (True, False):
        r.drv.play(CPU, [(GUARD, 0.0, 0.0, 300)], also_sub=also, ai_off=True)
        rows = r.record(61, nana=True)
        popo = {sname(x[CPU]["status"]) for x in rows[-30:]}
        nana = {sname(x["nana"]["status"]) for x in rows[-30:]}
        res.row(f"script guard, Nana driven too: {also}", "both Guard" if also else "Popo Guard, Nana not",
                f"Popo {sorted(popo)}, Nana {sorted(nana)}",
                popo == {"Guard"} and ((nana == {"Guard"}) if also else (nana != {"Guard"})))
    r.drv.stop()
    # Infinite shields: the script runs above have worn the shields down.
    r.opt.set(**on(behavior=BEHAVIOR["Shield"], infiniteShields=SHIELDS["On"], shieldHealth=50))
    rows = r.record(61, nana=True)
    popo = {sname(x[CPU]["status"]) for x in rows[-30:]}
    nana = {sname(x["nana"]["status"]) for x in rows[-30:]}
    pads = r.g.pad(sub)
    res.row("options Behavior Shield", "Popo and Nana Guard", f"Popo {sorted(popo)}, Nana {sorted(nana)}, "
            f"Nana's buttons {pads['buttons']:#x}", popo == {"Guard"} and nana == {"Guard"})
    r.opt.set(control=TRAINING)
    return res


def o_menu(r):
    """The CPU rows in the lab menu (the lab style: no wrap-around, the cursor
    stops on rows not built yet and skips disabled ones). Main Menu > CPU
    Options: set CPU Control to Lab, step Behavior to Shield; Tech Options:
    one step on Tech in Place Chance must rebalance the four to 100."""
    res = Result("menu rows")
    emu, lab = r.emu, LT.Lab(r.emu, [])
    lab.find_settings()
    r.opt.set(control=TRAINING, behavior=0, tech=0, techChance=[25, 25, 25, 25])
    r.drv.stop()
    # Over Training's pause: its opening and closing give the grab the
    # scenario before left on the CPU the frames to end.
    nav.pause_training(emu)
    lab.open_menu()
    lab.main_to(LT.MAIN_CPU)
    lab.tap(["A"])
    lab.down(CPU_CONTROL_ROW)
    lab.tap(stick=LT.RIGHT)
    control = r.opt.get("control")
    lab.down(CPU_BEHAVIOR_ROW - CPU_CONTROL_ROW)
    lab.tap(stick=LT.RIGHT)
    behavior = r.opt.get("behavior")
    shot = emu.screenshot("cpu-menu-options")[0]
    lab.up(CPU_BEHAVIOR_ROW - CPU_TECH_ROW)
    lab.tap(["A"])
    lab.down(TECH_IN_PLACE_ROW)
    lab.tap(stick=LT.RIGHT)
    chances = [r.opt.emu.u8(r.opt.base + SET_CPU + CPU_FIELDS["techChance"] + k) for k in range(4)]
    shot2 = emu.screenshot("cpu-menu-tech")[0]
    lab.back_to_main(2)
    lab.close_menu()
    nav.resume_training(emu)
    emu.advance(20)
    status = sname(r.g.fighter(r.g.entry(CPU))["status"])
    res.row("Right on CPU Control, Right on Behavior", "control 1 (Lab), behavior 1 (Shield)",
            f"control {control}, behavior {behavior}", control == LAB and behavior == 1)
    res.row("Right on Tech in Place Chance (25 each before)", "26, the others to a sum of 100",
            chances, chances[0] == 26 and sum(chances) == 100)
    res.row("after closing the menus the CPU shields", "Guard_On/Guard", status, status in ("Guard_On", "Guard"))
    res.note(f"screenshots: {shot}, {shot2}")
    r.opt.set(control=TRAINING, behavior=0, techChance=[25, 25, 25, 25])
    return res


def o_training(r):
    """CPU Control = Training leaves Training's own CPU option alone (its
    Shield option shields, the AI flag stays set, nothing is taken); with
    Lab, Training's Control option still wins (no pad on port 2: the CPU
    stands, it is not driven into Behavior Shield)."""
    res = Result("Training's CPU option")
    emu = r.emu
    out = {}
    for label, control, option in (("Training + Shield", TRAINING, "Shield"), ("Lab + Control", LAB, "Control")):
        pos = len(emu.log_text())
        r.load("start", {"control": control, "behavior": BEHAVIOR["Shield"]}, training=option)
        rows = r.record(90)
        takes = re.findall(r"cpu: take slot 1 \(P2 entry \w+, \w+\), flags 3", emu.log_text()[pos:])
        out[label] = (sname(rows[-1][CPU]["status"]), r.g.ai_flag(1), len(takes))
        res.note(f"{label}: last status {out[label][0]}, AI flag {out[label][1]}, takes by the options {len(takes)}")
    res.row("CPU Control Training, Training option Shield", "Guard, AI flag 1, no take",
            out["Training + Shield"], out["Training + Shield"] == ("Guard", 1, 0))
    res.row("CPU Control Lab, Training option Control", "not Guard, AI flag 0, no take",
            out["Lab + Control"], out["Lab + Control"][0] not in ("Guard", "Guard_On")
            and out["Lab + Control"][1:] == (0, 0))
    r.load("start", training="Attack")
    emu.h.call("resume")
    return res


# ---- counter actions (every list, every action) --------------------------------------

SPECIAL = 0x112  # the first fighter-specific status (specials)
ESCAPES = {STATUS["Escape_F"], STATUS["Escape_B"]}
WAITING = HIT | {STATUS["Wait"], STATUS["Damage_Fall"], STATUS["Guard_Damage"], STATUS["Guard"],
                 STATUS["Landing"], STATUS["Landing_Light"], STATUS["Squat"], STATUS["Squat_Wait"],
                 STATUS["Squat_Rv"]}
COUNTER_LOG = re.compile(r"cpu: P2 counter list (\d) action (\d+) \((\d+)\)")
LANDING_FALL_SPECIAL = 0x19


def moved(rows, k, frames=12):
    """x moved by the CPU from row k over `frames` rows, signed away from P1."""
    j = min(k + frames, len(rows) - 1)
    away = 1 if rows[k][CPU]["x"] > rows[k][P1]["x"] else -1
    return (rows[j][CPU]["x"] - rows[k][CPU]["x"]) * away


def judge(rows, start, spec):
    """The counter's statuses from row `start` (the first row of the action)
    against `spec`: (ok, text). Kinds:
      ("is", names)        the first status is one of `names`
      ("special",)         a fighter-specific status (>= 0x112) within 20 rows
      ("dir", names, d)    one of `names` first, moving away (d=1) / toward (-1)
      ("takeoff", after)   Jump_Squat, then on the first airborne row `after`
                           (the airdodge or aerial came out on its first
                           possible frame); with "special": >= 0x112 there
      ("hop", full)        Jump_Squat, then Jump; peak higher with a full hop
      ("dash", d, then)    Dash first, moving away / toward; then `then`
                           within 3 rows of it (None: nothing more)
      ("none",)            nothing is done (stays in a waiting status)."""
    if start is None:
        return spec[0] == "none", "no action"
    s0 = rows[start][CPU]["status"]
    kind = spec[0]
    seq = [sname(x[CPU]["status"]) for x in rows[start:start + 8]]
    if kind == "is":
        return sname(s0) in spec[1], f"{seq[:4]}"
    if kind == "jc":
        nxt = rows[start + 1][CPU]["status"] if start + 1 < len(rows) else None
        return (s0 == STATUS["Jump_Squat"] and nxt is not None and nxt >= SPECIAL,
                f"{seq[:3]} (jump squat's second frame)")
    if kind == "special":
        return s0 >= SPECIAL, f"{seq[:4]}"
    if kind == "dodge":
        return s0 == STATUS["Escape"], f"{seq[:3]}"
    if kind == "roll":
        m = moved(rows, start)
        ok = s0 in ESCAPES and (len(spec) < 2 or m * spec[1] > 3)
        return ok, f"{seq[:3]} moved {m:+.1f}"
    if kind == "dir":
        m = moved(rows, start)
        return sname(s0) in spec[1] and m * spec[2] > 3, f"{seq[:3]} moved {m:+.1f}"
    if kind == "takeoff":
        if s0 != STATUS["Jump_Squat"]:
            return False, f"{seq[:4]}"
        k = first(rows, start, lambda x: x[CPU]["status"] != STATUS["Jump_Squat"])
        after = rows[k][CPU]["status"] if k is not None else None
        ok = after is not None and (after >= SPECIAL if spec[1] == "special" else after == STATUS.get(spec[1], spec[1]))
        return ok, f"Jump_Squat x{(k or start) - start}, then {sname(after) if after is not None else None}"
    if kind == "hop":
        k = first(rows, start, lambda x: x[CPU]["status"] != STATUS["Jump_Squat"])
        peak = max(x[CPU]["y"] for x in rows[start:start + 60]) - rows[start][CPU]["y"]
        ok = s0 == STATUS["Jump_Squat"] and k is not None and rows[k][CPU]["status"] == STATUS["Jump"]
        return ok, f"{seq[:2]} peak {peak:.1f}", peak
    if kind == "dash":
        m = moved(rows, start)
        ok = s0 in (STATUS["Dash"], STATUS["Turn_Dash"]) and m * spec[1] > 3
        if spec[2] is not None:
            k = first(rows, start, lambda x: x[CPU]["status"] == STATUS[spec[2]])
            ok = ok and k is not None and k - start <= spec[3]
            seq = [sname(x[CPU]["status"]) for x in rows[start:start + 9]]
        return ok, f"{seq[:4]} moved {m:+.1f}"
    if kind == "none":
        return False, f"{seq[:4]}"
    return False, "?"


# Index -> (label, spec, starts with A/B (timed against the attack probe, not
# the guard probe)).
GROUND_ACTIONS = [
    (1, "Spotdodge", ("dodge",), False), (2, "Shield", ("is", ["Guard_On"]), False),
    (3, "Grab", ("is", ["Catch"]), True), (4, "Up B", ("special",), True),
    (5, "Side B Towards", ("special",), True), (6, "Down B", ("special",), True),
    (7, "Neutral B", ("special",), True), (8, "Up Smash", ("is", ["Attack_Hi4_Start"]), True),
    (9, "Down Smash", ("is", ["Attack_Lw4_Start"]), True), (10, "Forward Smash", ("is", ["Attack_S4_Start"]), True),
    (11, "Roll Away", ("roll", 1), False), (12, "Roll Towards", ("roll", -1), False),
    (13, "Jab", ("is", ["Attack"]), True),
    (14, "Forward Tilt", ("is", ["Attack_S3"]), True), (15, "Up Tilt", ("is", ["Attack_Hi3"]), True),
    (16, "Down Tilt", ("is", ["Attack_Lw3"]), True), (17, "Dash Attack", ("dash", -1, "Attack_Dash", 8), False),
    (18, "Short Hop", ("hop", False), False), (19, "Full Hop", ("hop", True), False),
    (20, "Wavedash Away", ("takeoff", LANDING_FALL_SPECIAL), False),
    (21, "Wavedash Towards", ("takeoff", LANDING_FALL_SPECIAL), False),
    (22, "Wavedash Down", ("takeoff", LANDING_FALL_SPECIAL), False),
    (23, "Dash Back", ("dash", 1, None, 0), False), (24, "Dash Through", ("dash", -1, None, 0), False),
    (32, "Side B Away", ("special",), True), (33, "Roll Random", ("roll",), False),
    (34, "Neutral Air", ("takeoff", "Attack_Air"), False), (35, "Forward Air", ("takeoff", "Attack_Air"), False),
    (36, "Back Air", ("takeoff", "Attack_Air"), False), (37, "Up Air", ("takeoff", "Attack_Air"), False),
    (38, "Down Air", ("takeoff", "Attack_Air"), False)]
# Indices 25-30 are Play Slot 1-6 and 31 Play Random Slot (the recording
# tests play them); 39 is Random Advanced, 40 Random (src/main/cpu/actions.cpp).
GROUND_RANDOM_POOL = {1, 2, 3, 11, 12, 13, 14, 15, 16, 18, 20, 23}
# An aerial jump, or the multiple jump of Kirby, Jigglypuff and the like (0xD).
AIR_JUMPS = ["Jump_Aerial", "0xd"]
AIR_ACTIONS_ALL = [
    (1, "Airdodge", ("is", ["Escape_Air"])), (2, "Jump Away", ("dir", AIR_JUMPS, 1)),
    (3, "Jump Towards", ("dir", AIR_JUMPS, -1)), (4, "Jump Neutral", ("is", AIR_JUMPS)),
    (5, "Up B Away", ("special",)), (6, "Up B Towards", ("special",)), (7, "Neutral B", ("special",)),
    (8, "Side B Away", ("special",)), (9, "Side B Towards", ("special",)), (10, "Down B", ("special",)),
    (11, "Neutral Air", ("is", ["Attack_Air"])), (12, "Forward Air", ("is", ["Attack_Air"])),
    (13, "Back Air", ("is", ["Attack_Air"])), (14, "Up Air", ("is", ["Attack_Air"])),
    (15, "Down Air", ("is", ["Attack_Air"]))]
AIR_RANDOM_POOL = {1, 2, 3, 11, 12, 13, 14, 15}
SHIELD_ACTIONS_ALL = [
    (1, "Grab", ("is", ["Catch"])), (2, "Short Hop", ("hop", False)), (3, "Full Hop", ("hop", True)),
    (4, "Spotdodge", ("is", ["Escape"])), (5, "Roll Away", ("dir", ["Escape_F", "Escape_B"], 1)),
    (6, "Roll Towards", ("dir", ["Escape_F", "Escape_B"], -1)), (7, "Up B", ("jc", "special")),
    (8, "Side B Away", ("takeoff", "special")), (9, "Side B Towards", ("takeoff", "special")),
    (10, "Down B", ("takeoff", "special")), (11, "Neutral B", ("takeoff", "special")),
    (12, "Up Smash", ("is", ["Jump_Squat"])), (13, "Down Smash", ("is", ["Guard_Off"])),
    (14, "Forward Smash", ("is", ["Guard_Off"])), (15, "Neutral Air", ("takeoff", "Attack_Air")),
    (16, "Forward Air", ("takeoff", "Attack_Air")), (17, "Back Air", ("takeoff", "Attack_Air")),
    (18, "Up Air", ("takeoff", "Attack_Air")), (19, "Down Air", ("takeoff", "Attack_Air")),
    (20, "Wavedash Away", ("takeoff", LANDING_FALL_SPECIAL)), (21, "Shield Drop", ("is", ["Guard_Off"])),
    (29, "Wavedash Towards", ("takeoff", LANDING_FALL_SPECIAL)),
    (30, "Wavedash Down", ("takeoff", LANDING_FALL_SPECIAL)), (31, "Roll Random", ("is", ["Escape_F", "Escape_B"]))]
# 22-27 Play Slot 1-6, 28 Play Random Slot, 32 Random Advanced, 33 Random.
SHIELD_RANDOM_POOL = {1, 2, 4, 5, 6, 7, 12, 15, 16, 20}


def action_start(rows, hit):
    return first(rows, hit + 1, lambda x: x[CPU]["status"] not in WAITING)


# A short press of A: a single jab (held longer, the jab combo hits a CPU
# that stays close again and again).
GROUND_JAB_HOLD = 3


# The grounded hit chosen for the match's CPU (ground_setup): None until
# chosen, then "jab" (percent) or "crouch".
GROUND_SETUP = {}


def ground_setup(r):
    """A hit that leaves the CPU on the ground through its hitstun, chosen
    per character: Mario's jab on the standing CPU at the first of 40, 20,
    10 and 0% that keeps it grounded (Fox); else his down tilt on the CPU
    crouching at 0%, the stick held down through the hitlag (crouch cancel
    and ASDI down: Landing, then Squat_Wait). A standing jab pops Bowser,
    Ice Climbers and Jigglypuff into Damage_Air, airborne on the first
    frame they can act."""
    if "kind" not in GROUND_SETUP:
        GROUND_SETUP.update(kind="crouch", percent=0)
        for pct in (40, 20, 10, 0):
            rows, hit = r.hit_run("near", JAB, 40, script=[NEUTRAL_STEP], percent=pct, hold=GROUND_JAB_HOLD)
            if not any(x[CPU]["air"] for x in rows[hit:hit + 30]):
                GROUND_SETUP.update(kind="jab", percent=pct)
                break
    return GROUND_SETUP


def ground_hit(r, frames, options=None, react=None, script=None, delay=0, extras=False):
    """The grounded hit of ground_setup. With options: for the crouching
    setup Behavior Crouch and ASDI Down. A probe's react should start after
    the hitlag (GROUND_FREE frames after the hit)."""
    g = ground_setup(r)
    if g["kind"] == "jab":
        return r.hit_run("near", JAB, frames, options, react=react,
                         script=script if script is not None or options is not None else [NEUTRAL_STEP],
                         percent=g["percent"], delay=delay, hold=GROUND_JAB_HOLD, extras=extras)
    if options is not None:
        options = dict(options, behavior=BEHAVIOR["Crouch"], asdi=ASDI["Down"])
    elif script is None:
        script = [CROUCH_STEP]
    return r.hit_run("crouch", DTILT, frames, options, react=react, script=script, percent=0, delay=delay,
                     hold=GROUND_JAB_HOLD, extras=extras)


GROUND_FREE = 6


def ground_grounded(rows, hit):
    """No airborne row from the end of the hitlag to 3 rows after the
    hitstun."""
    end = first(rows, hit + 1, lambda x: x[CPU]["hitlag"] == 0 and x[CPU]["hitstun"] == 0)
    stop = (end if end is not None else len(rows) - 4) + 4
    return not any(x[CPU]["air"] for x in rows[hit + 4:stop])


def o_actions_ground(r, only=None):
    res = Result("ground counter actions")
    rows, hit, act = ground_probe_guard(r)
    if act is None:
        raise CpuError("the guard probe never shielded")
    res.row("down tilt on the crouching CPU: grounded through the hitstun", "yes",
            "yes" if ground_grounded(rows, hit) else "no", ground_grounded(rows, hit))
    act -= hit
    res.note(f"first actionable frame (guard probe): hit +{act}")
    # Jump probe: jump tapped every other frame from the hit (two runs a
    # frame apart); the first Jump_Squat is the first frame a jump can start.
    jumps = []
    for phase in (0, 1):
        def react(i, h, phase=phase):
            if h is not None and i == h + GROUND_FREE + phase:
                return ([(JUMP, 0.0, 0.0, 1), (0, 0.0, 0.0, 1)], True)
            return None
        prow, ph = ground_hit(r, 50, react=react)
        j = first(prow, ph + 1, lambda x: x[CPU]["status"] == STATUS["Jump_Squat"])
        if j is not None:
            jumps.append(j - ph)
    # A jump starts on the same first frame as a held guard.
    res.row("jump probe: the first frame a jump starts", f"+{act}", f"+{min(jumps) if jumps else None}",
            jumps and min(jumps) == act)
    # Attack probe, the same way with A: the first Attack row is the first
    # frame an attack can start. After a standing hit's hitstun that is a
    # frame after a guard could; out of the Landing a crouch-cancelled hit
    # gives, every action opens on the same frame.
    attacks = []
    for phase in (0, 1):
        def react(i, h, phase=phase):
            if h is not None and i == h + GROUND_FREE + phase:
                return ([(A, 0.0, 0.0, 1), (0, 0.0, 0.0, 1)], True)
            return None
        prow, ph = ground_hit(r, 50, react=react)
        k = first(prow, ph + 1, lambda x: x[CPU]["status"] == STATUS["Attack"])
        if k is not None:
            attacks.append(k - ph)
    atk = min(attacks) if attacks else act + 1
    res.note(f"attack probe (A every other frame): first Attack at +{attacks} (phases 0, 1)")
    # Dash attack probe: the stick toward Mario, then A on dash frame n;
    # the smallest n giving Attack_Dash (not a forward smash) is the earliest.
    dash_n = None
    for n in range(1, 8):
        r.load("near")
        r.drv.play(CPU, [(0, -1.0, 0.0, n), (A, -1.0, 0.0, 1), (0, 0.0, 0.0, 40)], ai_off=True)
        prow = r.record(30)
        if any(x[CPU]["status"] == STATUS["Attack_Dash"] for x in prow):
            dash_n = n
            break
    res.note(f"dash attack probe: A on dash frame {None if dash_n is None else dash_n + 1} gives Attack_Dash first")
    dash_first = dash_n
    # Spotdodge and roll probes: guard and the stick pressed together (a
    # frame-perfect human) on each frame around the first actionable one;
    # the earliest Escape row is the earliest the game allows.
    escape_at = {}
    for kind, stick in (("dodge", (0.0, -1.0)), ("roll", (1.0, 0.0))):
        best = None
        for k in (act - 2, act - 1, act):
            def react(i, h, k=k, stick=stick):
                if h is not None and i == h + k:
                    return [(GUARD, stick[0], stick[1], 2), (0, 0.0, 0.0, 300)]
                return None
            prow, ph = ground_hit(r, 60, react=react)
            want_s = {STATUS["Escape"]} if kind == "dodge" else ESCAPES
            e = first(prow, ph + 1, lambda x: x[CPU]["status"] in want_s)
            if e is not None and (best is None or e - ph < best[0]):
                best = (e - ph, k, [sname(x[CPU]["status"]) for x in prow[e - 2:e + 1]])
        escape_at[kind] = best[0] if best else None
        res.note(f"{kind} probe (guard + stick pressed together): earliest at +{best[0] if best else None}"
                 f"{f' (pressed +{best[1]}, statuses {best[2]})' if best else ''}")
    hops = {}
    for idx, label, spec, ab in GROUND_ACTIONS:
        if only and idx not in only:
            continue
        rows, h = ground_hit(r, 110, on(counterGround=idx))
        k = action_start(rows, h)
        got = judge(rows, k, spec)
        ok, text = got[0], got[1]
        if spec[0] == "hop" and len(got) > 2:
            hops[spec[1]] = got[2]
        want = escape_at.get(spec[0]) or (atk if ab else act)
        timing = k is not None and k - h == want
        res.row(f"{idx} {label}", f"{spec[0]} {spec[1] if len(spec) > 1 else ''} at +{want}".strip(),
                f"{text} at +{None if k is None else k - h}", ok and timing)
        if idx == 17 and k is not None and dash_first is not None:
            d = first(rows, k, lambda x: x[CPU]["status"] == STATUS["Attack_Dash"])
            res.row("17 Dash Attack: Attack_Dash on the probe's earliest dash frame", f"dash +{dash_first}",
                    f"dash +{None if d is None else d - k}", d is not None and d - k == dash_first)
    if len(hops) == 2:
        res.row("full hop higher than short hop", "yes", f"{hops[False]:.1f} < {hops[True]:.1f}",
                hops[True] > hops[False] + 5)
    # Random: the choice (log) is in the random pool and is what happened.
    picks = set()
    for t in range(6):
        pos = len(r.emu.log_text())
        rows, h = ground_hit(r, 110, on(counterGround=40), delay=t)
        m = COUNTER_LOG.findall(r.emu.log_text()[pos:])
        chosen = int(m[0][1]) if m else None
        picks.add(chosen)
        spec = {a[0]: a[2] for a in GROUND_ACTIONS}.get(chosen)
        got = judge(rows, action_start(rows, h), spec) if spec is not None else (False, "")
        res.row(f"Random trial {t}: pick in the random pool and done", "yes", f"pick {chosen}: {got[1]}",
                chosen in GROUND_RANDOM_POOL and got[0])
    res.note(f"random picks: {sorted(p for p in picks if p is not None)}")
    return res


AIR_PERCENT = [60]


def air_percent(r):
    """A percent at which Mario's forward smash leaves the CPU in the air
    when its hitstun ends (a heavy one needs more)."""
    for pct in (60, 90, 120):
        rows, hit = r.hit_run("far", FSMASH, 160, script=[NEUTRAL_STEP], percent=pct)
        end = first(rows, hit + 1, lambda x: x[CPU]["hitstun"] == 0 and x[CPU]["hitlag"] == 0)
        if end is not None and rows[end][CPU]["air"] and rows[end][CPU]["status"] not in GONE:
            return pct
    return 60


def air_probe(r):
    firsts = []
    for phase in (0, 1):
        def react(i, hit, phase=phase):
            if hit is not None and i == hit + 1 + phase:
                return ([(GUARD, -1.0, 0.0, 1), (0, 0.0, 0.0, 1)], True)
            return None
        rows, hit = r.hit_run("far", FSMASH, 160, react=react, script=[NEUTRAL_STEP], percent=AIR_PERCENT[0])
        e = first(rows, hit + 1, lambda x: x[CPU]["status"] == STATUS["Escape_Air"])
        if e is not None:
            firsts.append(e - hit)
    return min(firsts) if firsts else None


def o_actions_air(r, only=None):
    res = Result("air counter actions")
    AIR_PERCENT[0] = air_percent(r)
    res.note(f"forward smash at {AIR_PERCENT[0]}% (the hitstun ends in the air)")
    act = air_probe(r)
    res.note(f"first airdodge possible (probe): hit +{act}")
    # The jumps' drift is judged against Jump Neutral's (the launch carries
    # the CPU away from Mario whatever it does).
    rows, h = r.hit_run("far", FSMASH, 160, on(counterAir=4), percent=AIR_PERCENT[0])
    k = first(rows, h + 1, lambda x: x[CPU]["status"] not in HIT)
    neutral = moved(rows, k) if k is not None else 0.0
    res.note(f"Jump Neutral moves {neutral:+.1f} away in 12 frames")
    for idx, label, spec in AIR_ACTIONS_ALL:
        if only and idx not in only:
            continue
        rows, h = r.hit_run("far", FSMASH, 160, on(counterAir=idx), percent=AIR_PERCENT[0])
        k = first(rows, h + 1, lambda x: x[CPU]["status"] not in HIT)
        if spec[0] == "dir" and k is not None:
            m = moved(rows, k) - neutral
            ok = sname(rows[k][CPU]["status"]) in spec[1] and m * spec[2] > 2
            text = f"{sname(rows[k][CPU]['status'])}, {m:+.1f} against Jump Neutral"
        else:
            ok, text = judge(rows, k, spec)[:2]
        res.row(f"{idx} {label}", f"{spec[0]} {spec[1] if len(spec) > 1 else ''} at +{act}".strip(),
                f"{text} at +{None if k is None else k - h}", ok and k is not None and k - h == act)
    # Probe: can a CPU fastfall in tumble at all? Down flicked every third
    # frame from the end of the hitstun (script, AI off).
    def react(i, h):
        if h is not None and act is not None and i == h + act:
            return ([(0, 0.0, -1.0, 1), (0, 0.0, 0.0, 2)], True)
        return None
    prow, ph = r.hit_run("far", FSMASH, 200, react=react, script=[NEUTRAL_STEP], percent=AIR_PERCENT[0])
    pv = [x[CPU]["y"] - y[CPU]["y"] for y, x in zip(prow, prow[1:])]
    pj = [i for i, (a, b) in enumerate(zip(pv, pv[1:])) if b - a < -0.3 and prow[i + 1][CPU]["air"] and i > ph]
    tumble_ff = bool(pj)
    res.note(f"probe, down flicks in tumble: fall speed jumps at rows {pj[:3]}; statuses {statuses(prow)[:5]}")
    # Fastfalls: after hitstun the fall speed jumps to the fastfall speed.
    for idx, label in ((16, "Tumble Fastfall"), (17, "Wiggle Fastfall")):
        rows, h = r.hit_run("far", FSMASH, 200, on(counterAir=idx), percent=AIR_PERCENT[0])
        vys = [x[CPU]["y"] - y[CPU]["y"] for y, x in zip(rows, rows[1:])]
        drop = [b - a for a, b in zip(vys, vys[1:])]
        big = [i for i, d in enumerate(drop) if d < -0.3 and rows[i + 1][CPU]["air"]]
        start = h + (act or 0)
        res.note(f"{label}: fall speed per row after the hitstun {[round(v, 2) for v in vys[start:start + 12]]}")
        out_of_tumble = any(x[CPU]["status"] == STATUS["Fall"] for x in rows[h:])
        want_fall = idx == 17
        if idx == 16 and not tumble_ff:
            # P+ gives no fastfall in tumble (the probe): the down flick
            # must change nothing either.
            res.row(f"{idx} {label}: no fastfall possible in tumble (probe), none made", "no jump",
                    f"jump at rows {big[:2]}", not big)
            continue
        res.row(f"{idx} {label}: a fastfall (fall speed jumps)", "yes" + (", leaves tumble" if want_fall else ""),
                f"jump at rows {big[:2]}, Fall seen {out_of_tumble}",
                bool(big) and (out_of_tumble if want_fall else True))
    for t in range(4):
        pos = len(r.emu.log_text())
        rows, h = r.hit_run("far", FSMASH, 160, on(counterAir=28), percent=AIR_PERCENT[0], delay=t)
        m = COUNTER_LOG.findall(r.emu.log_text()[pos:])
        chosen = int(m[0][1]) if m else None
        res.row(f"Random trial {t}: pick in the random pool", "yes", f"pick {chosen}", chosen in AIR_RANDOM_POOL)
    # Wait: no tech on landing. An up smash sends the CPU up so that its
    # hitstun ends more than the tech press's lead (5 frames) before it
    # lands; None (control) techs. With Tech Lockout Earliest the press
    # would come inside this hitstun, before the counter: Wait is honoured
    # from the hit on.
    land_st = {}
    for idx, name in ((19, "Wait"), (0, "None")):
        rows, h = r.hit_run("near", USMASH, 200, on(counterAir=idx, tech=TECH["In Place"]), percent=50)
        end = first(rows, h + 1, lambda x: x[CPU]["hitstun"] == 0 and x[CPU]["hitlag"] == 0)
        land = first(rows, h + 1, lambda x: not x[CPU]["air"])
        land_st[name] = (sname(rows[land][CPU]["status"]) if land else None,
                         (land - end) if land is not None and end is not None else None)
    res.row("19 Wait vs None (Tech In Place): landing long after hitstun", "Down vs Passive",
            f"{land_st['Wait']} vs {land_st['None']}",
            land_st["Wait"][0] == "Down" and land_st["None"][0] == "Passive" and (land_st["Wait"][1] or 0) > 5)
    return res


def shield_run(r, opts, frames=110, at=40, percent=0):
    """From 'near': Mario's jab (A for 3 frames, from row at + 1) on the
    CPU's shield. The script block presses it, frame-exact: the harness pad
    reached the game a row early or late, and held 2 or 3 rows, from one
    savestate to another."""
    r.load("near", opts, percent=percent)
    p1_input = r.g.inputs(r.g.entry(P1))[0]
    r.drv.play(P1, [(0, 0.0, 0.0, at + 1), (A, 0.0, 0.0, 3), (0, 0.0, 0.0, frames)])

    def each(i, rows):
        if rows:
            rows[-1]["p1pad"] = r.g.pad(p1_input)["buttons"]
    rows = r.record(frames, players=(CPU, P1), each=each, extras=True)
    r.drv.stop()
    stun = first(rows, 0, lambda x: x[CPU]["status"] == STATUS["Guard_Damage"])
    end = first(rows, stun or 0, lambda x: x[CPU]["status"] != STATUS["Guard_Damage"]) if stun else None
    return rows, stun, end


def pad_changes(rows):
    """[(row, buttons)] where Mario's buttons, as his input object holds
    them, change (shield_run's rows)."""
    out, last = [], None
    for q in rows:
        b = q.get("p1pad")
        if b is not None and b != last:
            out.append((q["i"], hex(b)))
            last = b
    return out[:12]


def o_actions_shield(r, only=None):
    res = Result("shield counter actions")
    rows, stun, end = shield_run(r, on(behavior=BEHAVIOR["Shield"], counterShield=0))
    if end is None:
        raise CpuError("the jab did not hit the shield")
    res.note(f"shield stun {stun}..{end} (None: {sname(rows[end][CPU]['status'])})")
    hops = {}
    for idx, label, spec in SHIELD_ACTIONS_ALL:
        if only and idx not in only:
            continue
        rows, stun, end = shield_run(r, on(behavior=BEHAVIOR["Shield"], counterShield=idx))
        got = judge(rows, end, spec)
        if spec[0] == "hop" and len(got) > 2:
            hops[spec[1]] = got[2]
        extra = ""
        ok = got[0]
        if idx in (13, 14):
            want = "Attack_Lw4_Start" if idx == 13 else "Attack_S4_Start"
            k = first(rows, end, lambda x: x[CPU]["status"] == STATUS[want])
            g = first(rows, end, lambda x: x[CPU]["status"] != STATUS["Guard_Off"])
            extra = (f", {want} at stun end +{None if k is None else k - end}, "
                     f"Guard_Off ends +{None if g is None else g - end}")
            ok = ok and k is not None and g is not None and k == g
        if idx == 12:
            k = first(rows, end, lambda x: x[CPU]["status"] == STATUS["Attack_Hi4_Start"])
            extra = f", up smash at +{None if k is None else k - end}"
            ok = ok and k == end + 1
        res.row(f"{idx} {label}", f"{spec[0]} {spec[1] if len(spec) > 1 else ''} at stun end".strip(),
                f"{got[1]}{extra}", ok and end is not None)
    if len(hops) == 2:
        res.row("out of shield: full hop higher than short hop", "yes", f"{hops[False]:.1f} < {hops[True]:.1f}",
                hops[True] > hops[False] + 5)
    for t in range(4):
        pos = len(r.emu.log_text())
        rows, stun, end = shield_run(r, on(behavior=BEHAVIOR["Shield"], counterShield=33), at=40 + t)
        m = COUNTER_LOG.findall(r.emu.log_text()[pos:])
        chosen = int(m[0][1]) if m else None
        res.row(f"Random trial {t}: pick in the random pool", "yes", f"pick {chosen}", chosen in SHIELD_RANDOM_POOL)
    return res


# The P+ character techniques at the end of the shield and ground lists
# (src/main/cpu/actions.cpp): which characters have them, and their indices.
# Peach and Yoshi cancel a double jump too, but from the ground they land two
# rows into the aerial: not offered (yoshi checks that they act as None).
DJC_CHARACTERS = {"ness", "lucas", "mewtwo"}
FLOAT_CHARACTERS = {"peach", "mewtwo", "ganondorf"}
AERIALS = ("Neutral Air", "Forward Air", "Back Air", "Up Air", "Down Air")
SHIELD_DJC, SHIELD_FLOAT, SHIELD_SHFFL = 34, 39, 44
GROUND_DJC, GROUND_FLOAT, GROUND_SHFFL = 41, 46, 51
SPIN_SHINE = {"fox", "falco", "wolf", "ness", "lucas"}
COUNTER_SHIELD_ROW = 12  # CPU Options, CPU Control Lab


# The aerials' subactions (AttackAirN, F, B, Hi, Lw), which Peach's and
# Mewtwo's float plays inside its own status (FitPeach.pac action 0x11E,
# FitMewtwo.pac action 0x120).
AERIAL_SUBS = (0x62, 0x63, 0x64, 0x65, 0x66)


def technique(rows, start, kind, sh_peak, aerial=0, char=None):
    """(ok, text) for a technique's rows from its first row `start`.
    DJC: Jump_Squat, the double jump on the first airborne row, the aerial on
    the next, the rise cancelled (the highest point under half a short
    hop's), and five rows or more of the aerial before landing. Float: Jump_Squat, Jump, the float (a fighter's own status), the
    aerial's subaction (rows with extras) on the row after the float first
    shows, low to the ground (Ganondorf enters 0x112 on the first airborne
    row and transitions into Attack_Air 0x33 after float startup)."""
    if start is None or rows[start][CPU]["status"] != STATUS["Jump_Squat"]:
        return False, f"{statuses(rows[start or 0:])[:4]}"
    k = first(rows, start, lambda x: x[CPU]["status"] != STATUS["Jump_Squat"])
    seq = [sname(x[CPU]["status"]) for x in rows[k:k + 6]]
    y0 = rows[start][CPU]["y"]
    if kind == "djc":
        peak = max(x[CPU]["y"] for x in rows[k:k + 40]) - y0
        land = first(rows, k, lambda x: not x[CPU]["air"])
        attack = sum(1 for x in rows[k:land] if x[CPU]["status"] == STATUS["Attack_Air"])
        hit = first(rows, k, lambda x: x[P1]["status"] in HIT)
        ok = (rows[k][CPU]["air"] and seq[0] == "Jump_Aerial" and seq[1] == "Attack_Air"
              and sh_peak is not None and peak < sh_peak / 2 and attack >= 5)
        return ok, (f"Jump_Squat x{k - start}, then {seq[:3]}, highest {peak:.1f} (short hop {sh_peak:.1f}), "
                    f"{attack} Attack_Air rows, then {sname(rows[land][CPU]['status']) if land else None}, "
                    f"Mario hit at +{None if hit is None else hit - k}")
    if char == "ganondorf":
        a = first(rows, k, lambda x: x[CPU]["status"] == STATUS["Attack_Air"])
        height = rows[a][CPU]["y"] - y0 if a is not None else None
        got = rows[a]["x"]["sub"] if a is not None else None
        cape = any(x[CPU]["status"] == 0x117 for x in rows[start:(a + 5 if a is not None else k + 20)])
        in_float = a is not None and all(x[CPU]["status"] == 0x112 for x in rows[k:a])
        trace = [sname(x[CPU]["status"]) for x in rows[k:(a + 2 if a is not None else k + 12)]]
        ok = (rows[k][CPU]["air"] and rows[k][CPU]["status"] == 0x112 and not cape
              and in_float and a == k + 6
              and got == AERIAL_SUBS[aerial] and height is not None and height < 6)
        return ok, (f"Jump_Squat x{k - start}, then {trace}, Attack_Air at +{None if a is None else a - k}, "
                    f"subaction {None if got is None else hex(got)}, height "
                    f"{None if height is None else round(height, 1)}, cape {cape}")
    f = first(rows, k, lambda x: x[CPU]["status"] >= SPECIAL)
    a = first(rows, k, lambda x: x["x"]["sub"] in AERIAL_SUBS)
    height = rows[a][CPU]["y"] - y0 if a is not None else None
    got = rows[a]["x"]["sub"] if a is not None else None
    ok = (seq[0] == "Jump" and f == k + 1 and a == f + 1 and got == AERIAL_SUBS[aerial]
          and rows[a][CPU]["status"] == rows[f][CPU]["status"] and height is not None and height < 6)
    return ok, (f"Jump_Squat x{k - start}, then {seq[:3]}, float at +{None if f is None else f - k}, subaction "
                f"{None if got is None else hex(got)} at +{None if a is None else a - k}, height "
                f"{None if height is None else round(height, 1)}")


def o_oos_techniques(r):
    """Shield counters on the match's CPU: Down B (the shine or magnet out of
    shield: Jump_Squat, then the special on the first airborne row), the
    double jump cancelled and float aerials where the character has them
    (and as None where it has not), and one of each on the ground."""
    name = r.cpu_name
    res = Result(f"P+ techniques ({name})")
    rows, stun, end = shield_run(r, on(behavior=BEHAVIOR["Shield"], counterShield=0))
    if end is None:
        raise CpuError("the jab did not hit the shield")
    none_seq = statuses(rows[end:end + 30])
    res.note(f"None: gap {rows[0][CPU]['x'] - rows[0][P1]['x']:.2f}, stun {stun}..{end}, CPU {statuses(rows)[:8]}, "
             f"Mario {statuses(rows, P1)[:10]}, Mario's buttons {pad_changes(rows)}")
    rows, stun, end = shield_run(r, on(behavior=BEHAVIOR["Shield"], counterShield=2))
    sh_peak = judge(rows, end, ("hop", False))[2] if end is not None else None
    if name in SPIN_SHINE:
        rows, stun, end = shield_run(r, on(behavior=BEHAVIOR["Shield"], counterShield=10))
        ok, text = judge(rows, end, ("takeoff", "special"))
        k = first(rows, end, lambda x: x[CPU]["status"] != STATUS["Jump_Squat"])
        hit = first(rows, end, lambda x: x[P1]["status"] in HIT)
        res.row("10 Down B: the down special on the first airborne row", "Jump_Squat, then special (air)",
                f"{text}, air {rows[k][CPU]['air'] if k is not None else None}",
                ok and k is not None and rows[k][CPU]["air"])
        res.note(f"10 Down B: Mario hit at stun end +{None if hit is None else hit - end}")
    for base, kind, have in ((SHIELD_DJC, "djc", name in DJC_CHARACTERS),
                             (SHIELD_FLOAT, "float", name in FLOAT_CHARACTERS)):
        for i, aerial in enumerate(AERIALS):
            idx = base + i
            rows, stun, end = shield_run(r, on(behavior=BEHAVIOR["Shield"], counterShield=idx))
            label = f"{idx} {'DJC' if kind == 'djc' else 'Float'} {aerial}"
            if have:
                ok, text = technique(rows, end, kind, sh_peak, i, char=name)
                res.row(label, "at stun end", text, ok and end is not None)
                if not ok:
                    res.note(f"{label}: stun {stun}..{end}, Mario {statuses(rows, P1)[:10]}, Mario's buttons "
                             f"{pad_changes(rows)}")
            else:
                seq = statuses(rows[end:end + 30]) if end is not None else None
                res.row(f"{label}: not this character's, acts as None", f"{none_seq[:3]}", f"{seq[:3]}",
                        seq is not None and [s for _, s in seq] == [s for _, s in none_seq])
    # The menu: Right on Counter Action (Shield) from Random goes to the
    # first technique only if the character has it (the others are grey).
    emu, lab = r.emu, LT.Lab(r.emu, [])
    lab.find_settings()
    r.opt.set(control=LAB, counterShield=33)
    r.drv.stop()
    lab.open_menu()
    lab.main_to(LT.MAIN_CPU)
    lab.tap(["A"])
    lab.down(COUNTER_SHIELD_ROW)
    lab.tap(stick=LT.RIGHT)
    got = r.opt.get("counterShield")
    lab.back_to_main(1)
    lab.close_menu()
    want = SHIELD_DJC if name in DJC_CHARACTERS else (SHIELD_FLOAT if name in FLOAT_CHARACTERS else SHIELD_SHFFL)
    res.row("menu: Right on Counter Action (Shield) from Random", want, got, got == want)
    r.opt.set(control=TRAINING, counterShield=0)
    for base, kind, have in ((GROUND_DJC, "djc", name in DJC_CHARACTERS),
                             (GROUND_FLOAT, "float", name in FLOAT_CHARACTERS)):
        if not have:
            continue
        rows, h = ground_hit(r, 110, on(counterGround=base), extras=True)
        ok, text = technique(rows, action_start(rows, h), kind, sh_peak, char=name)
        res.row(f"ground {base} {'DJC' if kind == 'djc' else 'Float'} Neutral Air", "after the hitstun", text, ok)
    return res


# ---- shield options --------------------------------------------------------------------

def o_shield_angle(r):
    res = Result("Shield Angle")
    for name in ("Neutral", "Up", "Down", "Towards", "Away"):
        r.load("near", on(behavior=BEHAVIOR["Shield"], shieldAngle=ANGLE[name], infiniteShields=SHIELDS["On"],
                          shieldHealth=50), percent=0)
        rows = r.record(50, players=(CPU, P1), extras=True)
        st = {sname(x[CPU]["status"]) for x in rows[8:]}
        tx, ty = rows[-1]["x"]["tilt"]
        toward = -1 if rows[-1][CPU]["x"] > rows[-1][P1]["x"] else 1
        want = {"Neutral": (0, 0), "Up": (0, 1), "Down": (0, -1), "Towards": (toward, 0), "Away": (-toward, 0)}[name]
        ok = abs(tx - want[0]) < 0.05 and abs(ty - want[1]) < 0.05 and st <= {"Guard_On", "Guard"}
        res.row(f"{name}: guard status work floats 4/5 (shield stick)", f"{want}, Guard only",
                f"({tx:.2f}, {ty:.2f}), {sorted(st)}", ok)
    # The tilt stays through shield stun and the shield counter still works.
    rows, stun, end = shield_run(r, on(behavior=BEHAVIOR["Shield"], shieldAngle=ANGLE["Up"], counterShield=1,
                                       infiniteShields=SHIELDS["On"], shieldHealth=50))
    tilt = rows[stun - 1]["x"]["tilt"] if stun else None
    res.row("Up: tilted before the hit, Grab after the stun", "(0, 1), Catch",
            f"{tilt}, {sname(rows[end][CPU]['status']) if end else None}",
            end is not None and abs(tilt[1] - 1) < 0.05 and rows[end][CPU]["status"] == STATUS["Catch"])
    return res


PS_LOG = re.compile(r"perfect shield[^\n]*")
PS_SHIELD = re.compile(r"shield ([-0-9.]+) ([-0-9.]+) r ([-0-9.]+)( fallback)?")
PS_DISTANCE = 30


def ps_save_state(r, name):
    """Pause and save a standing state in the current match for powershield checks."""
    r.drv.play(CPU, [NEUTRAL_STEP], loop=True)
    set_training_option(r.emu, "Attack")
    r.emu.h.call("pause")
    r.emu.step(frames=20)
    r.states.save(r.emu, name)
    r.emu.h.call("resume")


def ps_fire_b(r):
    def fire(i, rows):
        if i == 0:
            r.emu.hold(0, buttons=["B"])
        elif i == 3:
            r.emu.neutral(0)
    return fire


def ps_projectile_check(r, res, label, state, fire, frames=120, place=None, check_early=None, la_bit=True):
    """Compare Perfect Shield against Shield for a projectile or thrown item from P1.
    `fire(i, rows)` drives port 0. `place`, if given, is `(p1_x, cpu_x)`."""
    out = {}
    ps_rows = []
    ps_logs = []
    guarding = (STATUS["Guard_On"], STATUS["Guard"])
    for mode, opts in (("Perfect Shield", on(behavior=BEHAVIOR["Perfect Shield"], counterShield=0)),
                       ("Shield", on(behavior=BEHAVIOR["Shield"], counterShield=0,
                                     infiniteShields=SHIELDS["Until Hit"], shieldHealth=60))):
        pos = len(r.emu.log_text())
        r.load(state, opts)
        r.opt.cpu_percent(0, False)
        if place is not None:
            put_fighter(r, r.g.entry(P1), place[0], 0.0)
            put_fighter(r, r.g.entry(CPU), place[1], 0.0)
        rows = r.record(frames, players=(CPU, P1), each=fire, extras=True)
        r.emu.neutral(0)
        gd = first(rows, 0, lambda x: x[CPU]["status"] == STATUS["Guard_Damage"])
        on_ = first(rows, 0, lambda x: x[CPU]["status"] == STATUS["Guard_On"])
        refl_f = first(rows, 0, lambda x: x[CPU]["status"] in guarding and x["x"]["reflect"])
        hitc = first(rows, 0, lambda x: x[CPU]["status"] in HIT)
        contact = refl_f if refl_f is not None else gd
        psbit = rows[contact]["x"]["psbit"] if contact is not None else 0
        psdrop = rows[contact]["x"]["psdrop"] if contact is not None else 0
        drop = rows[gd - 1][CPU]["shield"] - rows[gd][CPU]["shield"] if gd else 0.0
        cpu_dmg = rows[-1][CPU]["damage"] - rows[0][CPU]["damage"]
        p1_dmg = rows[-1][P1]["damage"] - rows[0][P1]["damage"]
        said = [s.strip() for s in PS_LOG.findall(r.emu.log_text()[pos:])[:4]]
        out[mode] = (gd, on_, drop, hitc, cpu_dmg, p1_dmg, said, refl_f, psbit, psdrop)
        if mode == "Perfect Shield":
            ps_rows = rows
            ps_logs = said
        res.note(f"{label} ({mode}): CPU {statuses(rows)[:6]}; Guard_On {on_}, Guard_Damage {gd}, "
                 f"refl_f {refl_f}, psbit {psbit}, psdrop {psdrop}, hit {hitc}, drop {drop:.2f}, "
                 f"cpu_dmg {cpu_dmg:.1f}, p1_dmg {p1_dmg:.1f}; log {said}")
    gd, on_, drop, hitc, cpu_dmg, p1_dmg, said, refl_f, psbit, psdrop = out["Perfect Shield"]
    sh_gd, _, sh_drop, _, _, _, _, _, sh_psbit, sh_psdrop = out["Shield"]
    contact = refl_f if refl_f is not None else gd
    bubble = None
    for s in ps_logs:
        m = PS_SHIELD.search(s)
        if m:
            bubble = f"({m.group(1)}, {m.group(2)}) r={m.group(3)}{m.group(4) or ''}"
            break
    res.note(f"{label}: shield sphere used {bubble}, refl_f={refl_f}, psbit={psbit}, psdrop={psdrop}")
    # On a frame-1 rim powershield, Wait -> Guard_On -> Guard_Damage happens in one frame.
    g_at = on_ if on_ is not None else gd
    not_early = (g_at is not None and (g_at < 2 or ps_rows[g_at - 2][CPU]["status"] not in guarding)
                 and (check_early is None or g_at > check_early))
    # P+'s powershield window is Guard_On frames 0..1 (0 <= contact - g_at <= 1);
    # contact sits at or just after the 1.0x held-shield frame (0.76x reflect bubble).
    timed = (not_early and contact is not None and 0 <= contact - g_at <= 1
             and sh_gd is not None and sh_gd - 1 <= contact <= sh_gd + 4)
    res.row(f"{label}: guards in 2-frame window at contact, not early",
            f"0 <= contact - guard <= 1, contact in {sh_gd - 1 if sh_gd is not None else None}.."
            f"{sh_gd + 4 if sh_gd is not None else None}",
            f"guard {g_at}, contact {contact} (On {on_}, Dmg {gd}, refl_f {refl_f}), hit {hitc}, {bubble}", timed)
    ps_ok = ((gd is None and refl_f is not None)
             or (gd is not None and psdrop > 0 and (psbit == 1 or not la_bit) and drop < 0.5))
    blocked = (sh_gd is not None and sh_drop > 0.5 and sh_psbit == 0 and sh_psdrop == 0
               and cpu_dmg < 0.1 and hitc is None and ps_ok)
    flag_exp = "LA-Bit[96]=1 & RA-Basic[3]>0" if la_bit else "RA-Basic[3]>0"
    res.row(f"{label}: powershield flag & 0-dmg vs held shield",
            f"0 dmg & (RA-Bit[10] or ({flag_exp} & drop < 0.5)) vs held flags=0 & drop > 0.5",
            f"dmg {cpu_dmg:.1f}, p1_dmg {p1_dmg:.1f}, refl_f {refl_f}, psbit {psbit} vs {sh_psbit}, "
            f"psdrop {psdrop} vs {sh_psdrop}, drop {drop:.2f} vs {sh_drop:.2f}", blocked)
    return out


def o_powershield(r):
    """Behavior Perfect Shield against Mario's dash attack (the hitbox moves
    toward the CPU for a few frames before it reaches it), Mario's fireball,
    and Counter Action (Shield) after a reflected projectile."""
    res = Result("Perfect Shield")
    out = {}
    ps_rows = []
    for label, opts in (("Perfect Shield", on(behavior=BEHAVIOR["Perfect Shield"], counterShield=0)),
                        ("Shield", on(behavior=BEHAVIOR["Shield"], counterShield=0))):
        pos = len(r.emu.log_text())
        r.load("start", opts, percent=0)
        pressed = [None]

        def each(i, rows):
            if i == 0:
                r.emu.hold(0, stick=(228, 128))
            elif pressed[0] is None and rows and rows[-1][CPU]["x"] - rows[-1][P1]["x"] < PS_DISTANCE:
                # the dash attack from far enough that its hitbox is out
                # before it reaches the CPU
                r.emu.hold(0, buttons=["A"], stick=(228, 128))
                pressed[0] = i
            elif pressed[0] is not None and i == pressed[0] + 3:
                r.emu.neutral(0)
        rows = r.record(160, players=(CPU, P1), each=each, extras=True)
        gd = first(rows, 0, lambda x: x[CPU]["status"] == STATUS["Guard_Damage"])
        on_ = first(rows, 0, lambda x: x[CPU]["status"] == STATUS["Guard_On"])
        hitc = first(rows, 0, lambda x: x[CPU]["status"] in HIT)
        psbit = rows[gd]["x"]["psbit"] if gd is not None else 0
        psdrop = rows[gd]["x"]["psdrop"] if gd is not None else 0
        drop = rows[gd - 1][CPU]["shield"] - rows[gd][CPU]["shield"] if gd else None
        out[label] = (gd, on_, drop, hitc, psbit, psdrop)
        if label == "Perfect Shield":
            ps_rows = rows
        said = PS_LOG.findall(r.emu.log_text()[pos:])[:2]
        res.note(f"{label}: P1 {statuses(rows, P1)[:6]}; CPU {statuses(rows)[:6]}; Guard_On row {on_}, "
                 f"Guard_Damage row {gd}, psbit {psbit}, psdrop {psdrop}, shield drop {drop}; log {said}")
    gd, on_, drop, hitc, psbit, psdrop = out["Perfect Shield"]
    guarding = (STATUS["Guard_On"], STATUS["Guard"])
    before = [sname(x[CPU]["status"]) for x in ps_rows[gd - 3:gd + 1]] if gd else None
    res.row("dash attack on Perfect Shield: not guarding two frames before the hit, guard on it",
            "..., not guard, (Guard_On), Guard_Damage", before,
            gd is not None and ps_rows[gd - 2][CPU]["status"] not in guarding)
    res.row("no shield damage (powershield) vs a held shield",
            "LA-Bit[96]=1 & RA-Basic[3]>0 & drop < 0.5 vs held flags=0 & drop > 0.5",
            f"psbit {psbit} vs {out['Shield'][4]}, psdrop {psdrop} vs {out['Shield'][5]}, "
            f"drop {drop} vs {out['Shield'][2]}",
            drop is not None and drop < 0.5 and (out["Shield"][2] or 0) > 0.5
            and psbit == 1 and psdrop > 0 and out["Shield"][4] == 0 and out["Shield"][5] == 0)

    # Mario fireball from distance (P1 Mario vs CPU Fox).
    ps_projectile_check(r, res, "Mario fireball", "start", ps_fire_b(r), frames=120, place=(-25.0, 25.0))

    # Counter Action (Shield) = Grab after a reflected Mario fireball, delay 0 and delay 10.
    counter_catch = {}
    for delay in (0, 10):
        pos = len(r.emu.log_text())
        r.load("start", on(behavior=BEHAVIOR["Perfect Shield"], counterShield=SHIELD_ACTIONS["Grab"],
                           counterDelay=delay))
        r.opt.cpu_percent(0, False)
        put_fighter(r, r.g.entry(P1), -25.0, 0.0)
        put_fighter(r, r.g.entry(CPU), 25.0, 0.0)
        crows = r.record(120, players=(CPU, P1), each=ps_fire_b(r), extras=True)
        r.emu.neutral(0)
        c_on = first(crows, 0, lambda x: x[CPU]["status"] == STATUS["Guard_On"])
        c_refl_f = first(crows, 0, lambda x: x[CPU]["status"] in guarding and x["x"]["reflect"])
        c_catch = first(crows, 0, lambda x: x[CPU]["status"] == STATUS["Catch"])
        c_gd = first(crows, 0, lambda x: x[CPU]["status"] == STATUS["Guard_Damage"])
        c_dmg = crows[-1][CPU]["damage"] - crows[0][CPU]["damage"]
        c_p1 = crows[-1][P1]["damage"] - crows[0][P1]["damage"]
        c_logs = [s.strip() for s in PS_LOG.findall(r.emu.log_text()[pos:])[:4]]
        counter_catch[delay] = (c_on, c_refl_f, c_catch, c_gd, c_dmg, c_p1, crows)
        res.note(f"counter after reflect (delay {delay}): CPU {statuses(crows)[:6]}; Guard_On {c_on}, "
                 f"refl_f {c_refl_f}, Catch {c_catch}, Guard_Damage {c_gd}, cpu_dmg {c_dmg:.1f}, "
                 f"p1_dmg {c_p1:.1f}; log {c_logs}")
    c_on, c_refl_f, c_catch, c_gd, c_dmg, c_p1, _ = counter_catch[0]
    res.row("counter after reflected projectile: Grab right after Guard_On",
            "reflected, Catch <= Guard_On + 4",
            f"refl_f {c_refl_f}, Guard_On {c_on}, Catch {c_catch}, p1_dmg {c_p1:.1f}",
            c_refl_f is not None and c_gd is None and c_dmg < 0.1 and c_on is not None and c_catch is not None
            and c_on < c_catch <= c_on + 4)
    d_on, d_refl_f, d_catch, d_gd, d_dmg, d_p1, drows = counter_catch[10]
    held_delay = (d_refl_f is not None and d_catch is not None
                  and all(drows[k][CPU]["status"] in guarding for k in range(d_refl_f, d_catch)))
    res.row("counter after reflected projectile (delay 10): Grab 10 actionable frames later",
            f"reflected, Guard held, Catch at {None if c_catch is None else c_catch + 10}",
            f"refl_f {d_refl_f}, Guard_On {d_on}, Catch {d_catch}, held {held_delay}, p1_dmg {d_p1:.1f}",
            d_refl_f is not None and d_gd is None and d_dmg < 0.1 and d_p1 > 0.1 and held_delay
            and c_catch is not None and d_catch == c_catch + 10)
    return res


def o_powershield_cancel(r):
    """After a powershield (RA-Basic[3] > 0), P+ opens Ground_Attack /
    Ground_Special / Guard_On on frame 1 of Guard_Off (PowerShield.asm:110-124),
    so canAct() must let a dropped-shield attack (OOS Forward Smash) cancel
    Guard_Off immediately instead of waiting out all 15 frames as a normal
    shield drop does."""
    res = Result("Powershield Guard_Off cancel")
    got = {}
    for label, beh in (("Perfect Shield", BEHAVIOR["Perfect Shield"]), ("Shield", BEHAVIOR["Shield"])):
        r.load("start", on(behavior=beh, counterShield=14), percent=0)
        pressed = [None]

        def each(i, rows):
            if i == 0:
                r.emu.hold(0, stick=(228, 128))
            elif pressed[0] is None and rows and rows[-1][CPU]["x"] - rows[-1][P1]["x"] < PS_DISTANCE:
                r.emu.hold(0, buttons=["A"], stick=(228, 128))
                pressed[0] = i
            elif pressed[0] is not None and i == pressed[0] + 3:
                r.emu.neutral(0)
        rows = r.record(160, players=(CPU, P1), each=each, extras=True)
        r.emu.neutral(0)
        gd = first(rows, 0, lambda x: x[CPU]["status"] == STATUS["Guard_Damage"])
        goff = first(rows, gd or 0, lambda x: x[CPU]["status"] == STATUS["Guard_Off"]) if gd is not None else None
        fs = first(rows, goff or 0, lambda x: x[CPU]["status"] == STATUS["Attack_S4_Start"]) \
            if goff is not None else None
        psbit = rows[gd]["x"]["psbit"] if gd is not None else 0
        psdrop = rows[gd]["x"]["psdrop"] if gd is not None else 0
        dt = (fs - goff) if fs is not None and goff is not None else None
        got[label] = (gd, goff, fs, dt, psbit, psdrop)
        res.note(f"{label}: gd {gd} (psbit {psbit}, psdrop {psdrop}), Guard_Off {goff}, FSmash {fs} (+{dt}f); "
                 f"statuses {statuses(rows[gd or 0:])[:8]}")
    _, goff_ps, fs_ps, dt_ps, psbit_ps, psdrop_ps = got["Perfect Shield"]
    _, goff_sh, fs_sh, dt_sh, psbit_sh, _ = got["Shield"]
    res.row("Perfect Shield + OOS FSmash: cancels Guard_Off within 3 frames",
            "psbit 1, psdrop > 0, FSmash <= Guard_Off + 3",
            f"psbit {psbit_ps}, psdrop {psdrop_ps}, +{dt_ps}f",
            psbit_ps == 1 and psdrop_ps > 0 and dt_ps is not None and dt_ps <= 3)
    res.row("normal Shield + OOS FSmash: still waits out full Guard_Off",
            "psbit 0, FSmash >= Guard_Off + 13",
            f"psbit {psbit_sh}, +{dt_sh}f",
            psbit_sh == 0 and dt_sh is not None and dt_sh >= 13)
    return res


def o_powershield_fox(r):
    """Behavior Perfect Shield against Fox's laser from distance, CPU's own
    projectile not triggering it, and CPU's own projectile reflected back by
    Fox triggering it (Fox P1 vs Mario CPU on Final Destination)."""
    res = Result("Perfect Shield (Fox laser)")
    ps_save_state(r, "ps-fox")
    ps_projectile_check(r, res, "Fox laser", "ps-fox", ps_fire_b(r), frames=80, place=(-30.0, 30.0))

    # CPU's own projectile must NOT trigger Perfect Shield.
    pos = len(r.emu.log_text())
    r.load("ps-fox", on(behavior=BEHAVIOR["Perfect Shield"], counterShield=0))
    r.opt.cpu_percent(0, False)
    put_fighter(r, r.g.entry(P1), -60.0, 0.0)
    put_fighter(r, r.g.entry(CPU), 20.0, 0.0)
    r.drv.play(CPU, [(B, 0.0, 0.0, 2)], ai_off=True)
    own_rows = r.record(100, players=(CPU, P1))
    r.drv.stop()
    own_guard = first(own_rows, 0, lambda x: x[CPU]["status"] in (STATUS["Guard_On"], STATUS["Guard"]))
    own_special = first(own_rows, 0, lambda x: x[CPU]["status"] >= 0x112)
    own_logs = [s.strip() for s in PS_LOG.findall(r.emu.log_text()[pos:])[:3]]
    res.note(f"CPU own fireball: CPU {statuses(own_rows)[:6]}; special {own_special}, guard {own_guard}; log {own_logs}")
    res.row("CPU's own projectile does not trigger Perfect Shield",
            "fired special, no Guard_On, no log",
            f"special {own_special}, guard {own_guard}, logs {len(own_logs)}",
            own_special is not None and own_guard is None and not own_logs)

    # CPU's own projectile reflected back at it by Fox's Shine MUST trigger Perfect Shield.
    pos = len(r.emu.log_text())
    r.load("ps-fox", on(behavior=BEHAVIOR["Perfect Shield"], counterShield=0))
    r.opt.cpu_percent(0, False)
    put_fighter(r, r.g.entry(P1), -20.0, 0.0)
    put_fighter(r, r.g.entry(CPU), 20.0, 0.0)
    r.drv.play(CPU, [(B, 0.0, 0.0, 2)], ai_off=True)

    def fox_shine(i, rows):
        if i == 25:
            r.emu.hold(0, buttons=["B"], stick=(128, 0))
        elif i == 65:
            r.emu.neutral(0)
    ret_rows = r.record(130, players=(CPU, P1), each=fox_shine, extras=True)
    r.emu.neutral(0)
    r.drv.stop()
    guarding = (STATUS["Guard_On"], STATUS["Guard"])
    ret_early = first(ret_rows[:35], 0, lambda x: x[CPU]["status"] in guarding)
    ret_guard = first(ret_rows, 35, lambda x: x[CPU]["status"] in (STATUS["Guard_On"], STATUS["Guard_Damage"]))
    ret_refl_f = first(ret_rows, 35, lambda x: x[CPU]["status"] in guarding and x["x"]["reflect"])
    ret_gd = first(ret_rows, 35, lambda x: x[CPU]["status"] == STATUS["Guard_Damage"])
    ret_drop = ret_rows[ret_gd - 1][CPU]["shield"] - ret_rows[ret_gd][CPU]["shield"] if ret_gd else 0.0
    ret_hit = first(ret_rows, 0, lambda x: x[CPU]["status"] in HIT)
    ret_dmg = ret_rows[-1][CPU]["damage"] - ret_rows[0][CPU]["damage"]
    ret_logs = [s.strip() for s in PS_LOG.findall(r.emu.log_text()[pos:])[:4]]
    res.note(f"reflected own fireball: P1 {statuses(ret_rows, P1)[:6]}; CPU {statuses(ret_rows)[:8]}; "
             f"early {ret_early}, guard {ret_guard}, refl_f {ret_refl_f}, gd {ret_gd}, drop {ret_drop:.2f}, "
             f"hit {ret_hit}, dmg {ret_dmg:.1f}; log {ret_logs}")
    res.row("reflected own projectile returning to owner triggers Perfect Shield",
            "no early guard, RA-Bit[10] reflect in 2-frame window, drop < 0.5, 0 dmg",
            f"early {ret_early}, guard {ret_guard}, refl_f {ret_refl_f}, gd {ret_gd}, "
            f"drop {ret_drop:.2f}, hit {ret_hit}, dmg {ret_dmg:.1f}",
            ret_early is None and ret_guard is not None and ret_refl_f is not None
            and 0 <= ret_refl_f - ret_guard <= 1 and ret_gd is None and ret_drop < 0.5
            and ret_hit is None and ret_dmg < 0.1)
    return res


def o_powershield_item(r):
    """Behavior Perfect Shield against a thrown item: Peach plucks a turnip
    (must not trigger while held) and throws it at Fox CPU."""
    res = Result("Perfect Shield (Peach turnip)")
    ps_save_state(r, "ps-item")

    def fire_turnip(i, rows):
        if i == 0:
            r.emu.hold(0, buttons=["B"], stick=(128, 0))
        elif i == 3:
            r.emu.neutral(0)
        elif i == 45:
            r.emu.hold(0, buttons=["A"], stick=(255, 128))
        elif i == 48:
            r.emu.neutral(0)
    ps_projectile_check(r, res, "Peach turnip", "ps-item", fire_turnip, frames=130,
                        place=(-25.0, 25.0), check_early=45)
    return res


def make_ps_char(proj_label, place, frames):
    def run(r):
        res = Result(f"Perfect Shield ({r.cpu_name} vs {proj_label})")
        state = f"ps-{r.cpu_name}-{proj_label.split()[0].lower()}"
        ps_save_state(r, state)
        # FitYoshi.pac overrides GuardDamage without P+'s LA-Bit[96] hook, while
        # still setting RA-Basic[3] = IC-Basic[23079] on the powershield branch.
        ps_projectile_check(r, res, f"{proj_label} vs {r.cpu_name}", state, ps_fire_b(r),
                            frames=frames, place=place, la_bit=(r.cpu_name != "yoshi"))
        return res
    return run


# ---- DI ---------------------------------------------------------------------------------

def o_asdi(r):
    """ASDI of its own: TDI None (main stick neutral), ASDI Right / Up: the
    shift on the frame hitlag ends against Auto's, and the launch speed."""
    res = Result("ASDI")
    got = {}
    for name in ("Auto", "Right", "Up", "Left"):
        rows, hit = r.hit_run("far", FSMASH, 30, on(asdi=ASDI[name]), percent=60)
        end = first(rows, hit, lambda x: x[CPU]["hitlag"] == 0)
        dx = rows[end][CPU]["x"] - rows[end - 1][CPU]["x"]
        dy = rows[end][CPU]["y"] - rows[end - 1][CPU]["y"]
        ln = launch(rows, hit)
        got[name] = (dx, dy, ln)
        res.note(f"{name}: hitlag-end step ({dx:+.2f}, {dy:+.2f}), launch ({ln['vx']:.2f}, {ln['vy']:.2f})")
    a = got["Auto"]
    res.row("Right: extra shift right at hitlag end", "> +2", f"{got['Right'][0] - a[0]:+.2f}",
            got["Right"][0] - a[0] > 2)
    res.row("Left: extra shift left", "< -2", f"{got['Left'][0] - a[0]:+.2f}", got["Left"][0] - a[0] < -2)
    res.row("Up: extra shift up", "> +2", f"{got['Up'][1] - a[1]:+.2f}", got["Up"][1] - a[1] > 2)
    same = all(abs(got[n][2]["vx"] - a[2]["vx"]) < 0.05 and abs(got[n][2]["vy"] - a[2]["vy"]) < 0.05
               for n in ("Right", "Up", "Left"))
    res.row("launch speed unchanged (the main stick stays neutral)", "same", "same" if same else "differs", same)
    return res


def o_asdi_probe(r):
    """Which frame P+ reads ASDI from and which the trajectory DI: the
    script (AI off) pushes the stick right on one frame only, around the
    end of hitlag, and the shift and launch are compared with no push."""
    res = Result("ASDI / DI frames (probe)")
    out = {}
    for at in (None, -2, -1, 0, 1):
        def react(i, hit, at=at):
            if at is None or hit is None:
                return None
            # rows[hit] has hitlag H left; the frame with H frames to go ends it
            end = hit + react.h
            if i == end + at:
                return [(0, 1.0, 0.0, 1), (0, 0.0, 0.0, 300)]
            return None
        react.h = 9

        rows, hit = r.hit_run("far", FSMASH, 30, react=react, script=[NEUTRAL_STEP], percent=60)
        h = rows[hit][CPU]["hitlag"]
        end = first(rows, hit, lambda x: x[CPU]["hitlag"] == 0)
        step = rows[end][CPU]["x"] - rows[end - 1][CPU]["x"]
        ln = launch(rows, hit)
        out[at] = (step, ln["angle"])
        res.note(f"push at hitlag end {at}: hitlag {h}, end row {end - hit}, step at the end {step:+.2f}, "
                 f"launch {ln['angle']:.1f} deg")
    base = out[None]
    asdi = [a for a in (-2, -1, 0, 1) if abs(out[a][0] - base[0]) > 1]
    di = [a for a in (-2, -1, 0, 1) if abs(out[a][1] - base[1]) > 2]
    res.row("frames whose push moves the hitlag-end step (ASDI) / turns the launch (DI)", "both found",
            f"ASDI {asdi}, DI {di}", bool(asdi) and bool(di))
    return res


DI_LOG = re.compile(r"cpu: P2 hit (\d+): hitlag \d+ hitstun \d+, di (-?\d+),(-?\d+) sdi")


def o_tdi_measured(r):
    """Inwards/Outwards perpendicular to the knockback the game gave (read
    from memory at hitlag), on a forward smash and on a down smash."""
    res = Result("Trajectory DI measured")
    for state, smash, pct in (("far", FSMASH, 60), ("near", DSMASH, 50)):
        ang = {}
        for name in ("None", "Inwards", "Outwards"):
            pos = len(r.emu.log_text())
            rows, hit = r.hit_run(state, smash, 40, on(tdi=TDI[name]), percent=pct, extras=True)
            kb = rows[hit + 1]["x"]["kb"]
            m = DI_LOG.findall(r.emu.log_text()[pos:])
            di = (int(m[-1][1]) / 100, int(m[-1][2]) / 100) if m else (0, 0)
            ln = launch(rows, hit)
            ang[name] = ln["angle"]
            if name != "None":
                dot = (kb[0] * di[0] + kb[1] * di[1]) / max(math.hypot(*kb), 1e-6)
                toward = 1 if rows[hit][P1]["x"] > rows[hit][CPU]["x"] else -1
                # inwards: the DI's x points toward the opponent for an upward launch
                side = di[0] * toward
                want_side = 1 if (name == "Inwards") == (kb[1] >= 0) else -1
                res.row(f"{smash} {name}: DI perpendicular to the knockback {tuple(round(v, 2) for v in kb)}",
                        f"dot 0, x toward the {'opponent' if want_side > 0 else 'outside'}",
                        f"di {di}, dot {dot:.2f}", abs(dot) < 0.05 and side * want_side > 0)
            res.note(f"{smash} {name}: launch {ln['angle']:.1f} deg")
        res.row(f"{smash}: launch angle Inwards / None / Outwards differ the right way", "In > None > Out (up launch)",
                f"{ang['Inwards']:.1f} / {ang['None']:.1f} / {ang['Outwards']:.1f}",
                ang["Inwards"] > ang["None"] + 2 > ang["Outwards"] + 4)
    return res


# ---- cues and holds (src/main/cpu/cue.h) ------------------------------------------------

C_STICK = A | 0x20 | 0x100  # attack + smash + C-stick, as a player's C-stick arrives
LIST_GROUND, LIST_AIR = 0, 1
REF_ABS, REF_TOWARD = 0, 1
# Ground list index, label, the action's first inputs (script steps), the
# status it starts.
CUE_GROUND = [(3, "Grab", [(A | GUARD, 0.0, 0.0, 1)], ["Catch"]),
              (8, "Up Smash", [(C_STICK, 0.0, 1.0, 1)], ["Attack_Hi4_Start"]),
              (13, "Jab", [(A, 0.0, 0.0, 1)], ["Attack"]),
              (1, "Spotdodge", [(GUARD, 0.0, -1.0, 2)], ["Escape"]),
              (18, "Short Hop", [(JUMP, 0.0, 0.0, 1)], ["Jump_Squat"])]
JAB_INDEX, WAVEDASH_AWAY_INDEX, SPOTDODGE_INDEX, ROLL_AWAY_INDEX = 13, 20, 1, 11
HOP_STEP = (JUMP, 0.0, 0.0, 1)


def cue_air(toward, lr):
    """The air list's actions as cue_ground's, the sticks as the CPU's
    scripts point them (toward Mario, or the way the CPU faces)."""
    return [(1, "Airdodge", [(GUARD, toward, 0.0, 1)], ["Escape_Air"]),
            (4, "Jump Neutral", [(JUMP, 0.0, 0.0, 1)], AIR_JUMPS),
            (11, "Neutral Air", [(A, 0.0, 0.0, 1)], ["Attack_Air"]),
            (12, "Forward Air", [(C_STICK, lr, 0.0, 1)], ["Attack_Air"]),
            (15, "Down Air", [(C_STICK, 0.0, -1.0, 1)], ["Attack_Air"])]


def first_status(rows, names, start=0):
    return first(rows, start, lambda x: sname(x[CPU]["status"]) in names)


def cue_run(r, ops, frames, setup=None, options=None):
    """From 'near' with the CPU Options on: `setup` script steps (AI off)
    from row 0 if given, and cue block operations {row: kwargs} written
    before those rows. Returns the rows and the block's last status."""
    r.load("near", options or on(), percent=0)
    if setup:
        r.drv.play(CPU, setup, ai_off=True)

    def each(i, rows):
        if i in ops:
            r.cue.op(**ops[i])
    rows = r.record(frames, each=each)
    return rows, r.cue.status()


def script_run(r, steps, frames):
    """The same start with the CPU Options off and the CPU played by
    `steps` (AI off) from row 0: a frame-perfect human."""
    r.load("near", percent=0)
    r.drv.play(CPU, steps, ai_off=True)
    return r.record(frames)


def presses(start, total, button=A, every=2):
    """Script steps: nothing, then `button` on driven frames start,
    start + every, ... up to `total`."""
    fr = {n: (button, 0.0, 0.0) for n in range(start, total + 1, every)}
    return PI.frames_to_steps(fr, total)


def o_cue(r):
    res = Result("Cues")
    st0 = r.g.fighter(r.g.entry(CPU))
    r.load("near", percent=0)
    rows0 = r.record(1, players=(CPU, P1))
    toward = 1.0 if rows0[0][P1]["x"] > rows0[0][CPU]["x"] else -1.0
    lr = rows0[0][CPU]["lr"]
    res.note(f"the CPU stands at {rows0[0][CPU]['x']:.1f} facing {lr:+.0f}, Mario on the {'right' if toward > 0 else 'left'}"
             f" (status before the load {sname(st0['status'])})")
    # Standing, as soon as it can: the first frame the cue runs in, as a
    # human pressing on that frame.
    for idx, label, steps, want in CUE_GROUND:
        rows, st = cue_run(r, {0: dict(op=CueBlock.CUE, lst=LIST_GROUND, index=idx)}, 30)
        k = first_status(rows, want)
        ref = script_run(r, steps + [(0, 0.0, 0.0, 40)], 30)
        kr = first_status(ref, want)
        res.row(f"standing, as soon as it can: {label} ({'/'.join(want)})", f"row {kr} (script)",
                f"row {k}, cue {st['state']}, start - op {st['start'] - st['at']}",
                k is not None and k == kr and st["state"] == "Done" and st["start"] == st["at"])
    # Standing, with a frame: the 8th frame from the one the cue runs in.
    rows, st = cue_run(r, {0: dict(op=CueBlock.CUE, lst=LIST_GROUND, index=JAB_INDEX, delay=8)}, 30)
    k = first_status(rows, ["Attack"])
    ref = script_run(r, [(0, 0.0, 0.0, 7), (A, 0.0, 0.0, 1), (0, 0.0, 0.0, 30)], 30)
    kr = first_status(ref, ["Attack"])
    res.row("standing, Jab cued for the 8th frame", f"row {kr} (script: A on frame 8)",
            f"row {k}, start - op {st['start'] - st['at']}",
            k is not None and k == kr and st["start"] - st["at"] == 7 and st["state"] == "Done")
    # Airborne: a short hop played by the script (one frame of jump), then
    # the CPU Options; the air cue, given while the CPU is still in its jump
    # squat, starts on the frame it leaves the squat (the first airborne
    # one), where a human's input comes out at the earliest.
    hop = script_run(r, [HOP_STEP, (0, 0.0, 0.0, 80)], 40)
    squat = first_status(hop, ["Jump_Squat"])
    take = first(hop, (squat or 0) + 1, lambda x: x[CPU]["status"] != STATUS["Jump_Squat"])
    res.note(f"short hop: Jump_Squat rows {squat}..{take - 1 if take else None}, airborne from row {take}")
    if take is None:
        raise CpuError("the short hop never left its jump squat")
    for idx, label, steps, want in cue_air(toward, lr):
        rows, st = cue_run(r, {0: dict(op=CueBlock.CUE, lst=LIST_AIR, index=idx)}, 40, setup=[HOP_STEP])
        sq = first_status(rows, ["Jump_Squat"])
        k = first_status(rows, want, sq or 0)
        d = None if k is None or sq is None else k - sq
        # The scripted human, counted from its own jump squat's first row
        # (a load can shift every row by one): the action pressed d rows
        # after it must come out there, and pressed a row sooner, no sooner.
        ref = {}
        for lead in (0, 1):
            ref[lead] = None
            if d is None:
                continue
            r.load("near", percent=0)
            r.drv.play(CPU, [HOP_STEP, (0, 0.0, 0.0, 80)], ai_off=True)
            mark = [None]

            def each(i, rr, lead=lead):
                if mark[0] is None and rr and rr[-1][CPU]["status"] == STATUS["Jump_Squat"]:
                    mark[0] = len(rr) - 1
                if mark[0] is not None and i == mark[0] + d - lead:
                    r.drv.play(CPU, steps + [(0, 0.0, 0.0, 40)], ai_off=True)
            rr = r.record(40, each=each)
            kr = first_status(rr, want, mark[0] or 0)
            ref[lead] = None if kr is None or mark[0] is None else kr - mark[0]
        seq = [sname(x[CPU]["status"]) for x in rows[(sq or 0):(sq or 0) + 5]]
        res.row(f"airborne, as soon as it can: {label} (rows from the jump squat)",
                f"+{d} as a script pressing there, none sooner a row before",
                f"cue +{d} {seq}, script +{ref[0]} / pressed a row before +{ref[1]}, cue {st['state']}",
                d is not None and ref[0] == d and (ref[1] is None or ref[1] >= d) and st["state"] in ("Done", "Running"))
    # Cancel: a waiting cue, then a running one (a wavedash cut after its
    # jump: no air dodge); and the same wavedash not cancelled.
    rows, st = cue_run(r, {0: dict(op=CueBlock.CUE, lst=LIST_GROUND, index=JAB_INDEX, delay=12),
                           5: dict(op=CueBlock.CANCEL)}, 30)
    k = first_status(rows, ["Attack"])
    res.row("Jab cued for frame 12, cancelled on frame 6", "no Attack, cue None", f"Attack at {k}, cue {st['state']}",
            k is None and st["state"] == "None")
    landed = {}
    for cancel in (False, True):
        ops = {0: dict(op=CueBlock.CUE, lst=LIST_GROUND, index=WAVEDASH_AWAY_INDEX)}
        if cancel:
            ops[1] = dict(op=CueBlock.CANCEL)
        rows, st = cue_run(r, ops, 60, )
        names = {sname(x[CPU]["status"]) for x in rows}
        landed[cancel] = ("Escape_Air" in names or "0x19" in names, "Jump" in names, st["state"])
    res.row("Wavedash Away: done / cancelled in its jump squat", "air dodge / a jump without one, cue None",
            f"{landed[False]} / {landed[True]}",
            landed[False][0] and landed[False][2] == "Done" and not landed[True][0] and landed[True][1]
            and landed[True][2] == "None")
    # After a hit: a cue given in the hitstun takes the counter action's
    # place (Spotdodge here) and starts on the first frame an attack can;
    # one given while the counter runs waits for it, then starts on the
    # first frame a human's A comes out after the spotdodge.
    attacks = []
    for phase in (0, 1):
        def react(i, h, phase=phase):
            if h is not None and i == h + GROUND_FREE + phase:
                return ([(A, 0.0, 0.0, 1), (0, 0.0, 0.0, 1)], True)
            return None
        prow, ph = ground_hit(r, 50, react=react)
        k = first(prow, ph + 1, lambda x: x[CPU]["status"] == STATUS["Attack"])
        if k is not None:
            attacks.append(k - ph)
    atk = min(attacks) if attacks else None
    counter = on(counterGround=SPOTDODGE_INDEX)
    rows, hit = ground_hit(r, 90, counter)
    dodge = first(rows, hit + 1, lambda x: x[CPU]["status"] == STATUS["Escape"])
    res.note(f"counter Spotdodge alone: Escape at hit +{None if dodge is None else dodge - hit}; "
             f"attack probe: first Attack at +{attacks}")

    def cue_at(row):
        def react(i, h):
            if h is not None and i == h + row:
                r.cue.op(CueBlock.CUE, lst=LIST_GROUND, index=JAB_INDEX)
            return None
        return react
    rows, hit = ground_hit(r, 90, counter, react=cue_at(1))
    k = first(rows, hit + 1, lambda x: x[CPU]["status"] == STATUS["Attack"])
    esc = first(rows, hit + 1, lambda x: x[CPU]["status"] == STATUS["Escape"])
    res.row("cue Jab given in the hitstun (counter Spotdodge)", f"Attack at +{atk}, no Escape",
            f"Attack at +{None if k is None else k - hit}, Escape at {esc}, cue {r.cue.status()['state']}",
            k is not None and atk is not None and k - hit == atk and esc is None)
    if dodge is not None:
        d = dodge - hit
        want = []
        for phase in (0, 1):
            # Neutral from the frame the CPU Options let go of the stick
            # after the hit (the probes' start), the spotdodge on the
            # counter's frame, then A every other frame.
            def react(i, h, phase=phase):
                if h is not None and i == h + GROUND_FREE:
                    o = d - GROUND_FREE
                    fr = {o + 1: (GUARD, 0.0, -1.0), o + 2: (GUARD, 0.0, -1.0)}
                    fr.update({n: (A, 0.0, 0.0) for n in range(o + 6 + phase, o + 61, 2)})
                    return PI.frames_to_steps(fr, o + 60)
                return None
            prow, ph = ground_hit(r, 100, react=react)
            e = first(prow, ph + 1, lambda x: x[CPU]["status"] == STATUS["Escape"])
            k = first(prow, (e or ph) + 1, lambda x: x[CPU]["status"] == STATUS["Attack"])
            res.note(f"script, spotdodge pressed at hit +{d}, A every other frame from +{d + 5 + phase}: Escape at "
                     f"+{None if e is None else e - ph}, Attack at +{None if k is None else k - ph}; "
                     f"{[(x['i'] - ph, sname(x[CPU]['status'])) for x in prow[ph + d - 1:ph + d + 1]]} "
                     f"{statuses(prow[ph:])[:8]}")
            if k is not None and e == ph + d:
                want.append(k - ph)
        rows, hit = ground_hit(r, 100, counter, react=cue_at(d + 2))
        e = first(rows, hit + 1, lambda x: x[CPU]["status"] == STATUS["Escape"])
        k = first(rows, (e or hit) + 1, lambda x: x[CPU]["status"] == STATUS["Attack"])
        res.row("cue Jab given while the counter Spotdodge runs", f"Escape at +{d}, then Attack at +{min(want) if want else None}"
                " (script)", f"Escape at +{None if e is None else e - hit}, Attack at +{None if k is None else k - hit}, "
                f"cue {r.cue.status()['state']}",
                want and e is not None and e - hit == d and k is not None and k - hit == min(want))
    # A cue given while the one before runs: Roll Away, then a Jab, which
    # waits for the roll and starts on the first frame a script's A comes
    # out after it.
    rows, st = cue_run(r, {0: dict(op=CueBlock.CUE, lst=LIST_GROUND, index=ROLL_AWAY_INDEX),
                           3: dict(op=CueBlock.CUE, lst=LIST_GROUND, index=JAB_INDEX)}, 70)
    roll = first(rows, 0, lambda x: x[CPU]["status"] in ESCAPES)
    k = first(rows, 0, lambda x: x[CPU]["status"] == STATUS["Attack"])
    want = []
    for phase in (0, 1):
        fr = {1: (GUARD, -toward, 0.0), 2: (GUARD, -toward, 0.0)}
        fr.update({n: (A, 0.0, 0.0) for n in range(6 + phase, 70, 2)})
        ref = script_run(r, PI.frames_to_steps(fr, 70), 70)
        kr = first(ref, 0, lambda x: x[CPU]["status"] == STATUS["Attack"])
        if kr is not None and first(ref, 0, lambda x: x[CPU]["status"] in ESCAPES) == roll:
            want.append(kr)
    res.row("Roll Away cued, then a Jab while it rolls", f"roll at {roll}, Attack at {min(want) if want else None}"
            " (script)", f"Attack at {k}, cue {st['state']}", want and k == min(want) and st["state"] == "Done")
    # Holds: walk toward Mario; shield, then released (the Behavior,
    # Stand, again); A with repeat against A held.
    rows, _ = cue_run(r, {0: dict(op=CueBlock.HOLD, x=0.6, ref=REF_TOWARD)}, 30)
    moved_in = (rows[-1][CPU]["x"] - rows[0][CPU]["x"]) * toward
    res.row("hold the stick toward Mario", "walks toward him", f"{statuses(rows)[:3]}, moved {moved_in:+.1f}",
            moved_in > 3 and any(x[CPU]["status"] == STATUS["Walk"] for x in rows))
    rows, _ = cue_run(r, {0: dict(op=CueBlock.HOLD, buttons=GUARD), 30: dict(op=CueBlock.RELEASE)}, 70)
    guard = all(x[CPU]["status"] in (STATUS["Guard_On"], STATUS["Guard"]) for x in rows[2:30])
    res.row("hold guard, released on frame 31", "shields, then stands", f"{statuses(rows)}",
            guard and rows[-1][CPU]["status"] == STATUS["Wait"])
    jumps = {}
    for op in (CueBlock.HOLD, CueBlock.HOLD_REPEAT):
        rows, _ = cue_run(r, {0: dict(op=op, buttons=JUMP)}, 60)
        jumps[op] = sum(1 for _, n in statuses(rows) if n in ("Jump_Squat", "Jump_Aerial", "0xd"))
    res.row("hold jump / with repeat: jumps started in 60 frames", "1 / more",
            f"{jumps[CueBlock.HOLD]} / {jumps[CueBlock.HOLD_REPEAT]}",
            jumps[CueBlock.HOLD] == 1 and jumps[CueBlock.HOLD_REPEAT] > 1)
    # With no cue and no hold, nothing changed: the block's last state.
    r.load("near", on(), percent=0)
    r.cue.op(CueBlock.RELEASE)
    r.record(2)
    res.row("after a release: no cue", "None", r.cue.status()["state"], r.cue.status()["state"] == "None")
    return res


LANDINGS = [("empty short hop", [HOP_STEP, (0, 0.0, 0.0, 80)]),
            ("full hop", [(JUMP, 0.0, 0.0, 6), (0, 0.0, 0.0, 80)]),
            ("short hop, fastfall", [HOP_STEP, (0, 0.0, 0.0, 14), (0, 0.0, -1.0, 1), (0, 0.0, 0.0, 80)]),
            ("short hop, double jump", [HOP_STEP, (0, 0.0, 0.0, 6), HOP_STEP, (0, 0.0, 0.0, 80)])]


def o_landing_light(r):
    """Landings without an attack or an air dodge: which give LandingLight
    (0x17). On the first such landing (or Landing if none does), a Jab cued
    in the air starts on the first frame a script's A comes out (the game's
    rule: parameter 3073 for LandingLight, 3074 for Landing)."""
    res = Result("Landing")
    pick = None
    for name, steps in LANDINGS:
        rows = script_run(r, steps, 90)
        up = first(rows, 0, lambda x: x[CPU]["air"])
        land = first(rows, (up or 0) + 1, lambda x: not x[CPU]["air"])
        st = sname(rows[land][CPU]["status"]) if land is not None else None
        res.note(f"{name}: lands on row {land} in {st}")
        if land is not None and (pick is None or (st == "Landing_Light" and pick[3] != "Landing_Light")):
            pick = (name, steps, land, st)
    lights = pick is not None and pick[3] == "Landing_Light"
    res.note(f"LandingLight reached by one of these landings: {'yes' if lights else 'no'}")
    if pick is None:
        raise CpuError("no landing")
    name, steps, land, st = pick
    res.note(f"used: {name} ({st})")
    want = []
    for phase in (0, 1):
        frames = {}
        n = 1
        for b_, x_, y_, k_ in steps:
            for _ in range(k_):
                frames[n] = (b_, x_, y_)
                n += 1
        frames.update({n_: (A, 0.0, 0.0) for n_ in range(land + 2 + phase, land + 30, 2)})
        ref = script_run(r, PI.frames_to_steps(frames, land + 30), land + 30)
        k = first(ref, land, lambda x: x[CPU]["status"] == STATUS["Attack"])
        res.note(f"script A from row {land + 1 + phase}: Attack on row {k}")
        if k is not None:
            want.append(k)
    rows, st_ = cue_run(r, {land - 3: dict(op=CueBlock.CUE, lst=LIST_GROUND, index=JAB_INDEX)}, land + 30,
                        setup=steps[:-1])
    k = first(rows, land - 3, lambda x: x[CPU]["status"] == STATUS["Attack"])
    seen = [(x["i"], sname(x[CPU]["status"])) for x in rows[land - 3:land + 12]]
    res.note(f"cue run around the landing: {seen}; cue {st_}")
    res.row(f"{name}: Jab cued in the air, first Attack row", f"{min(want) if want else None} (script A every "
            "other frame)", f"{k}", bool(want) and k == min(want) and k > land)
    return res


# ---- the game's rules (notes from its code), each against a scripted human ---------------

def shield_probe(r, steps, frames=110, opts=None, cue=False):
    """'near', the CPU shielding: with `opts`, the CPU Options' Behavior
    Shield; without, a script holding guard. P1's jab (harness pad, its
    row may vary by a frame or two) hits the shield; on the row after the
    first GuardDamage row `steps` are played, or with `cue` a Jab is cued.
    Returns the rows and that hit row."""
    r.load("near", opts, percent=0)
    if opts is None:
        r.drv.play(CPU, [(GUARD, 0.0, 0.0, 1)], loop=True, ai_off=True)
    hit = [None]

    def each(i, rows):
        if i == 40:
            r.emu.hold(0, buttons=["A"])
        elif i == 43:
            r.emu.neutral(0)
        if hit[0] is None and rows and rows[-1][CPU]["status"] == STATUS["Guard_Damage"]:
            hit[0] = len(rows) - 1
            if cue:
                r.cue.op(CueBlock.CUE, lst=LIST_GROUND, index=JAB_INDEX)
            elif steps:
                r.drv.play(CPU, steps, ai_off=True)
    rows = r.record(frames, players=(CPU, P1), each=each)
    if hit[0] is None:
        raise CpuError("the jab did not hit the shield")
    return rows, hit[0]


def o_shield_end(r):
    """The end of shield stun (GuardDamage: to Guard with shield held, to
    GuardOff without; GuardOff opens only jumps before its own end). For
    four out-of-shield actions, the counter's first row against a script
    holding shield and pressing the action every other frame from the row
    after the hit (two phases): the earliest a human gets it. Then a Jab
    cued from Behavior Shield (shield let go) against a script letting go
    of shield then and pressing A every other frame. Rows count from each
    run's own hit."""
    res = Result("End of shield stun")
    rows, stun, end = shield_run(r, on(behavior=BEHAVIOR["Shield"], counterShield=0))
    if end is None:
        raise CpuError("the jab did not hit the shield")
    res.note(f"shield stun rows {stun}..{end - 1}, then {sname(rows[end][CPU]['status'])}")
    away = 1.0 if rows[end][CPU]["x"] > rows[end][P1]["x"] else -1.0
    cases = [(1, "Grab", (A | GUARD, 0.0, 0.0), ["Catch"]), (4, "Spotdodge", (GUARD, 0.0, -1.0), ["Escape"]),
             (5, "Roll Away", (GUARD, away, 0.0), ["Escape_F", "Escape_B"]),
             (2, "Short Hop", (GUARD | JUMP, 0.0, 0.0), ["Jump_Squat"])]
    for idx, label, press, want in cases:
        crow, cstun, cend = shield_run(r, on(behavior=BEHAVIOR["Shield"], counterShield=idx))
        k = first_status(crow, want, cstun or 0)
        k = None if k is None or cstun is None else k - cstun
        human = []
        for phase in (0, 1):
            fr = {n: (press if n % 2 == phase else (GUARD, 0.0, 0.0)) for n in range(1, 61)}
            prow, ph = shield_probe(r, PI.frames_to_steps(fr, 60))
            kh = first_status(prow, want, ph)
            if kh is not None:
                human.append(kh - ph)
        res.row(f"{label} out of shield: first row after the hit", f"+{min(human) if human else None} (script)",
                f"+{k}", bool(human) and k == min(human))
    human = []
    for phase in (0, 1):
        fr = {n: ((A, 0.0, 0.0) if n % 2 == phase else (0, 0.0, 0.0)) for n in range(1, 61)}
        prow, ph = shield_probe(r, PI.frames_to_steps(fr, 60))
        kh = first_status(prow, ["Attack"], ph)
        if kh is not None:
            human.append(kh - ph)
    crow, ch = shield_probe(r, None, opts=on(behavior=BEHAVIOR["Shield"]), cue=True)
    k = first_status(crow, ["Attack"], ch)
    seq = statuses(crow)[:8]
    res.row("Jab cued in shield stun (shield let go): first Attack after the hit",
            f"+{min(human) if human else None} (script)", f"+{None if k is None else k - ch} {seq}",
            bool(human) and k is not None and k - ch == min(human))
    return res


def o_wiggle(r):
    """The wiggle out of tumble (DamageFly/DamageFall scripts): with hitstun
    over, a push of |x| >= 0.75 that left the 0.35 dead zone on this frame
    or the one before puts the fighter in Fall. Scripted pushes from the
    first row whose hitstun reads 0 (e): held full from e - 6 (no wiggle),
    full from e + 2 (wiggle), half from e + 2 (no wiggle), full from e - 1
    (the push starts on the last hitstun frame: wiggle per the rule)."""
    res = Result("Wiggle out of tumble")
    AIR_PERCENT[0] = air_percent(r)
    rows, hit = r.hit_run("far", FSMASH, 120, script=[NEUTRAL_STEP], percent=AIR_PERCENT[0])
    e = first(rows, hit + 1, lambda x: x[CPU]["hitstun"] == 0 and x[CPU]["hitlag"] == 0)
    if e is None:
        raise CpuError("the hitstun did not end")
    res.note(f"hitstun over from row {e} (hit {hit}), statuses {statuses(rows[hit:])[:4]}")
    for start, x, want in ((-6, 1.0, False), (2, 1.0, True), (2, 0.5, False), (-1, 1.0, True)):
        def react(i, h, start=start, x=x):
            return [(0, x, 0.0, 40), NEUTRAL_STEP] if h is not None and i == e + start else None
        prow, ph = r.hit_run("far", FSMASH, 120, react=react, script=[NEUTRAL_STEP], percent=AIR_PERCENT[0])
        fall = first(prow, e - 8, lambda xx: xx[CPU]["status"] == STATUS["Fall"])
        res.row(f"push {x} from hitstun end {start:+d}: Fall", "yes" if want else "no",
                f"{'row ' + str(fall) if fall is not None else 'no'}", (fall is not None) == want)
    return res


def o_air_jump(r):
    """Out of hitstun in the air a jump opens on the first frame out of
    hitstun even while still rising (P+ Hitstun.asm:149 Allow Interrupt).
    Up smash at 50% (falling by hitstun's end), and the same hit boosted
    upward before hitstun ends so vertical speed is still positive: the
    first air dodge and jump a script gets, and Jump Neutral's jump."""
    res = Result("Air jump after hitstun")
    firsts = {}
    for name, press, want in (("air dodge", (GUARD, -1.0, 0.0), ["Escape_Air"]),
                              ("jump", (JUMP, 0.0, 0.0), AIR_JUMPS)):
        got = []
        for phase in (0, 1):
            def react(i, h, phase=phase, press=press):
                if h is not None and i == h + 1 + phase:
                    return ([press + (1,), (0, 0.0, 0.0, 1)], True)
                return None
            prow, ph = r.hit_run("near", USMASH, 160, react=react, script=[NEUTRAL_STEP], percent=50)
            k = first_status(prow, want, ph + 1)
            if k is not None:
                got.append(k - ph)
                if name == "jump":
                    res.note(f"jump probe phase {phase}: vertical speed before it "
                             f"{prow[k - 1][CPU]['y'] - prow[k - 2][CPU]['y']:+.2f}")
        firsts[name] = min(got) if got else None
    rows, h = r.hit_run("near", USMASH, 160, on(counterAir=AIR_ACTIONS["Jump Neutral"]), percent=50)
    k = first_status(rows, AIR_JUMPS, h + 1)
    res.note(f"script: first air dodge +{firsts['air dodge']}, first jump +{firsts['jump']}")
    res.row("counter Jump Neutral after an up smash: first jump", f"+{firsts['jump']} (script)",
            f"+{None if k is None else k - h}", k is not None and firsts["jump"] is not None and k - h == firsts["jump"])

    # Rising at hitstun's end: boost vertical knockback 8 frames before hitstun
    # ends so the CPU is still in Damage_Fly with positive vertical speed.
    en = r.g.entry(CPU)
    e = first(rows, h + 1, lambda x: x[CPU]["hitstun"] == 0 and x[CPU]["hitlag"] == 0)
    if e is None:
        raise CpuError("hitstun did not end after up smash")
    boost_at = e - 8
    boost_hex = struct.pack(">ff", 0.0, 4.5).hex()
    rise_got, rise_vy = [], []
    for phase in (0, 1):
        def react_rise(i, hh, phase=phase):
            if i == boost_at:
                r.emu.h.call("write_mem", addr=kb_addr(r, en), hex=boost_hex)
            if hh is not None and i == hh + 1 + phase:
                return ([(JUMP, 0.0, 0.0, 1), (0, 0.0, 0.0, 1)], True)
            return None
        prow, ph = r.hit_run("near", USMASH, 160, react=react_rise, script=[NEUTRAL_STEP], percent=50)
        kk = first_status(prow, AIR_JUMPS, ph + 1)
        if kk is not None:
            vy = prow[kk - 1][CPU]["y"] - prow[kk - 2][CPU]["y"]
            rise_got.append(kk - ph)
            rise_vy.append(vy)
            res.note(f"rising jump probe phase {phase}: +{kk - ph} (status before {sname(prow[kk - 1][CPU]['status'])}, "
                     f"vy {vy:+.2f})")
    rise_first = min(rise_got) if rise_got else None
    rows_r, hr = r.hit_run("near", USMASH, 160, on(counterAir=AIR_ACTIONS["Jump Neutral"]),
                           react=lambda i, hh: (r.emu.h.call("write_mem", addr=kb_addr(r, en), hex=boost_hex), None)[1]
                           if i == boost_at else None, percent=50)
    kr = first_status(rows_r, AIR_JUMPS, hr + 1)
    vy_r = (rows_r[kr - 1][CPU]["y"] - rows_r[kr - 2][CPU]["y"]) if kr is not None and kr >= 2 else None
    st_r = sname(rows_r[kr - 1][CPU]["status"]) if kr is not None and kr >= 1 else None
    res.note(f"rising counter Jump Neutral: +{None if kr is None else kr - hr} (status before {st_r}, "
             f"vy {vy_r:+.2f})" if vy_r is not None else "rising counter Jump Neutral: none")
    res.row("counter Jump Neutral while still rising: first jump",
            f"+{rise_first} (script, vy > 0)",
            f"+{None if kr is None else kr - hr} (vy {vy_r:+.2f})" if vy_r is not None else "none",
            kr is not None and rise_first is not None and kr - hr == rise_first and vy_r is not None and vy_r > 0.25)
    return res


def o_air_jump_empty(r):
    """Counter Action (Air) Jump Neutral / Away / Towards with no aerial jump
    left: wiggles out of tumble by wiggling out of tumble into Fall
    instead of retrying X for 60-270 frames and blocking recovery."""
    res = Result("Air jump counter with no jump left")
    en = r.g.entry(CPU)
    jump_idx = {"Jump Neutral": 4, "Jump Away": 2, "Jump Towards": 3}

    def juggle_empty(name, offstage=False):
        # Hit 1 (USMASH at 50%): Fox burns its double jump via Jump Neutral.
        # Hit 2 (USMASH at 60%, held airborne beside Mario so jumps never
        # reset): Fox enters tumble with 0 aerial jumps left under `name`.
        r.load("near", on(counterAir=jump_idx["Jump Neutral"], recovery=RECOVERY["Off"]), percent=50)
        f0 = r.g.fighter(en)
        hx, hy = f0["x"], f0["y"]
        m = {"hit1": None, "dj": None, "press2": None, "hit2": None, "free2": None, "moved": False}
        flags = []

        def each(i, rows):
            if i == 0:
                r.emu.hold(0, buttons=["A"], stick=USMASH)
            elif i == 5:
                r.emu.neutral(0)
            if len(rows) >= 2:
                a, b = rows[-2][CPU], rows[-1][CPU]
                k = len(rows) - 1
                if m["hit1"] is None and b["hitlag"] > a["hitlag"] and b["status"] in HIT:
                    m["hit1"] = k
                if m["hit1"] is not None and m["dj"] is None and b["status"] in (STATUS["Jump_Aerial"], 0xD):
                    m["dj"] = k
                    r.opt.set(**on(counterAir=jump_idx[name],
                                   recovery=RECOVERY["Game"] if offstage else RECOVERY["Off"]))
                if m["dj"] is not None and m["hit2"] is None:
                    # Keep Fox airborne beside Mario until hit 2 connects so
                    # ground contact never restores its double jump.
                    put_cpu(r, en, hx, hy + 6.0)
                    if m["press2"] is None and rows[-1][P1]["status"] == STATUS["Wait"]:
                        r.opt.cpu_percent(60, True)
                        r.emu.hold(0, buttons=["A"], stick=USMASH)
                        m["press2"] = i
                if m["press2"] is not None and i == m["press2"] + 5:
                    r.emu.neutral(0)
                if m["press2"] is not None and m["hit2"] is None and b["hitlag"] > a["hitlag"] and b["status"] in HIT:
                    m["hit2"] = k
                if m["hit2"] is not None and not m["moved"] and b["hitlag"] == 0:
                    m["moved"] = True
                    put_cpu(r, en, 105.0 if offstage else 0.0, 100.0)
                    r.emu.h.call("write_mem", addr=kb_addr(r, en), hex=struct.pack(">ff", 0.0, 2.5).hex())
                if m["hit2"] is not None and m["free2"] is None and b["hitlag"] == 0 and b["hitstun"] == 0:
                    m["free2"] = k
            flags.append(r.g.ai_flag(en["slot"]))

        rows = r.record(240, players=(CPU, P1), each=each)
        r.emu.neutral(0)
        return rows, m, flags

    for name in ("Jump Neutral", "Jump Away", "Jump Towards"):
        rows, m, _ = juggle_empty(name, offstage=False)
        e = m["free2"]
        fall = first(rows, e, lambda x: x[CPU]["status"] == STATUS["Fall"]) if e is not None else None
        res.note(f"{name}: hit1 {m['hit1']}, dj {m['dj']}, hit2 {m['hit2']}, free2 {e}, fall {fall} "
                 f"(+{fall - e if fall is not None and e is not None else None}), "
                 f"statuses {statuses(rows[e or 0:])[:10]}")
        res.row(f"{name} with 0 jumps: wiggles to Fall on first free frame",
                "dj used, Fall within +2 of free2",
                f"dj {m['dj']}, +{fall - e if fall is not None and e is not None else None}",
                m["dj"] is not None and e is not None and fall is not None and fall - e <= 2)

    for name in ("Jump Neutral", "Jump Away"):
        rows, m, flags = juggle_empty(name, offstage=True)
        e = m["free2"]
        ai_on = next((k for k in range(e, len(flags)) if flags[k] == 1), None) if e is not None else None
        fall = first(rows, e, lambda x: x[CPU]["status"] == STATUS["Fall"]) if e is not None else None
        res.note(f"offstage {name}: dj {m['dj']}, hit2 {m['hit2']}, free2 {e}, fall {fall}, "
                 f"AI on {ai_on} (+{ai_on - e if ai_on is not None and e is not None else None}), "
                 f"statuses {statuses(rows[e or 0:])[:10]}")
        res.row(f"offstage {name} with 0 jumps: Fall + Game AI handoff",
                "Fall <= +2, AI flag 1 within +6",
                f"Fall +{fall - e if fall is not None and e is not None else None}, "
                f"AI +{ai_on - e if ai_on is not None and e is not None else None}",
                m["dj"] is not None and e is not None and fall is not None and fall - e <= 2
                and ai_on is not None and ai_on - e <= 6)
    return res


# The blast zones of Final Destination (measured: notes, DI display research X11).
FD_ZONES = (-246.0, 246.0, 188.0, -140.0)
KB_DECAY = 0.051
COS18, SIN18 = math.cos(math.radians(18)), math.sin(math.radians(18))


def gravity_of(r, en):
    """(fall acceleration, largest fall speed) of the kinetic module's
    gravity energy (id 1, +0x58), or None."""
    e = r.emu
    ft = en["fighters"][en["active"]]
    kin = e.u32(e.u32(e.u32(ft + 0x60) + 0xD8) + 0x7C)
    if e.u32(kin + 0x5C) != 1:
        return None
    g = e.u32(kin + 0x58)
    return e.f32(g + 0x10), e.f32(g + 0x14)


def di_turn(k, sx, sy):
    """Degrees (counter-clockwise) the launch `k` turns for stick (sx, sy):
    18 times c squared, c the stick's part across the launch."""
    n = math.hypot(sx, sy)
    if n > 1.0025:
        sx, sy = sx * 1.0025 / n, sy * 1.0025 / n
    c = max(-1.0, min(1.0, (k[0] * sy - k[1] * sx) / math.hypot(*k)))
    return 18.0 * c * abs(c)


def flight_margin(x, y, k, turn, frames, accel, max_fall):
    """The smallest distance to an FD blast zone along the flight of launch
    k turned 18 degrees by `turn` (+1 counter-clockwise), as the CPU
    Options follow it."""
    speed = math.hypot(*k)
    t = turn * SIN18
    dx, dy = (k[0] * COS18 - k[1] * t) / speed, (k[1] * COS18 + k[0] * t) / speed
    g, m = 0.0, 1e5
    for _ in range(min(frames, 150)):
        speed = speed - KB_DECAY if speed > KB_DECAY else 0.0
        g = max(g + accel, -max_fall)
        x += dx * speed
        y += dy * speed + g
        m = min(m, FD_ZONES[1] - x, x - FD_ZONES[0], FD_ZONES[2] - y, y - FD_ZONES[3])
    return m


def wrap(a):
    return (a + 180.0) % 360.0 - 180.0


TDI_CHOICES = ["None", "Inwards", "Outwards", "Survival"]


def o_tdi_angles(r):
    """For three hits with different launches (a forward smash, an up
    smash, and a down smash on the CPU put behind Mario, which sends it the
    other way), each trajectory DI choice:
    the change of the launch angle, from the knockback speed in memory on
    the last hitlag frame (before DI) and on the launch frame (after it),
    against the change the choice should give (DI rule: 18 degrees times
    c squared)."""
    res = Result("Trajectory DI angles")
    en = r.g.entry(CPU)
    grav = None
    # The down smash hits the CPU put behind Mario: a launch the other way.
    for state, smash, pct, hold in (("far", FSMASH, 60, 5), ("behind", DSMASH, 60, 5), ("near", USMASH, 50, 5)):
        for name in TDI_CHOICES:
            opts = on(tdi=TDI[name], asdi=ASDI["Auto"])
            pos = len(r.emu.log_text())
            def behind(i, h):
                if i == 0:
                    p1, me = r.g.fighter(r.g.entry(P1)), r.g.fighter(en)
                    put_cpu(r, en, 2 * p1["x"] - me["x"], me["y"])
                return None
            rows, hit = r.hit_run("near" if state == "behind" else state, smash, 45, opts, percent=pct, extras=True,
                                  hold=hold, react=behind if state == "behind" else None)
            grav = grav or gravity_of(r, en)
            end = first(rows, hit + 1, lambda x: x[CPU]["hitlag"] == 0)
            if end is None:
                res.check(f"{smash} {name}: no launch", False)
                continue
            pre, post = rows[end - 1]["x"]["kb"], rows[end]["x"]["kb"]
            got = wrap(math.degrees(math.atan2(post[1], post[0]) - math.atan2(pre[1], pre[0])))
            c = rows[end - 1][CPU]
            toward = 1.0 if rows[end - 1][P1]["x"] > c["x"] else -1.0
            inward = -toward if pre[1] >= 0 else toward
            m = DI_LOG.findall(r.emu.log_text()[pos:])
            stick = (int(m[-1][1]), int(m[-1][2])) if m else None
            either = False
            if name == "None":
                want = 0.0
            elif name in ("Inwards", "Outwards"):
                want = {"Inwards": 18.0, "Outwards": -18.0}[name] * inward
            else:
                up = 1.0 if pre[0] >= 0 else -1.0
                acc, mf = grav if grav else (-0.1, 3.0)
                m_up = flight_margin(c["x"], c["y"], pre, up, c["hitstun"], acc, mf)
                m_other = flight_margin(c["x"], c["y"], pre, -up, c["hitstun"], acc, mf)
                surv = -up if m_other > m_up + 0.01 else up
                either = abs(m_other - m_up) < 1.0
                want = 18.0 * surv
                res.note(f"{smash} {name}: flight margins turning toward up {m_up:.1f}, the other way "
                         f"{m_other:.1f}")
            ok = abs(got - want) < 0.25 or (either and abs(abs(got) - 18.0) < 0.25)
            res.row(f"{smash} {pct}% {name}: launch {math.degrees(math.atan2(pre[1], pre[0])):.1f} deg turns by",
                    f"{want:+.2f}", f"{got:+.2f} (stick {stick})", ok)
    return res


# ---- lab menu helpers -----------------------------------------------------------------

CPU_ROWS = {"advanced": 13, "move": 20, "controlled by": 21, "freeze": 22}


def cpu_page(r, row, then=None):
    """Open the lab menu, CPU Options, cursor to `row`, then `then(lab)`
    (default: A), back out and resume."""
    emu, lab = r.emu, LT.Lab(r.emu, [])
    lab.find_settings()
    r.drv.stop()
    lab.open_menu()
    lab.main_to(LT.MAIN_CPU)
    lab.tap(["A"])
    emu.press(0, [], frames=50, after=4, stick=LT.UP)
    lab.down(row)
    (then or (lambda lab_: lab_.tap(["A"])))(lab)
    lab.back_to_main(1)
    lab.close_menu()
    return lab


def o_move_freeze(r):
    res = Result("Move, Freeze, Controlled By")
    emu = r.emu
    r.load("start", on())
    emu.h.call("resume")
    en, p1 = r.g.entry(CPU), r.g.entry(P1)
    # What P1's input object holds for this harness stick, before Move CPU
    # takes it over (P1 walks a few frames).
    p1in = r.g.inputs(p1)[0]
    emu.hold(0, stick=(28, 228))
    emu.advance(4)
    sx, sy = r.g.pad(p1in)["x"], r.g.pad(p1in)["y"]
    emu.neutral(0)
    r.load("start", on())
    emu.h.call("resume")
    cpu_page(r, CPU_ROWS["move"])
    x0, y0 = r.g.fighter(en)["x"], r.g.fighter(en)["y"]
    px0 = r.g.fighter(p1)["x"]
    emu.h.call("pause")
    emu.hold(0, stick=(28, 228))
    xs = emu.trace(30, emu.watch(lambda _: (lambda f: (f["x"], f["y"], sname(f["status"])))(r.g.fighter(en))))
    emu.neutral(0)
    emu.h.call("resume")
    dx = xs[-1][0] - xs[9][0]
    dy = xs[-1][1] - xs[9][1]
    # P1's stick (as its input object held it) times kMoveSpeed (1.6), 20 frames.
    want = (sx * 1.6 * 20, sy * 1.6 * 20)
    pmoved = r.g.fighter(p1)["x"] - px0
    res.row("Move CPU: the CPU moves 1.6 times P1's stick a frame (20 frames)",
            f"({want[0]:+.1f}, {want[1]:+.1f})", f"({dx:+.1f}, {dy:+.1f}), {xs[-1][2]}",
            abs(dx - want[0]) < 1.5 and abs(dy - want[1]) < 1.5 and want[0] < -10)
    res.row("Move CPU: the player stays put", "0", f"{pmoved:+.1f}", abs(pmoved) < 1)
    cpu_page(r, CPU_ROWS["move"])
    emu.advance(90)
    f = r.g.fighter(en)
    res.row("Finish Moving: the CPU falls and stands again", "Wait on the ground",
            f"{sname(f['status'])}, y {f['y']:.1f}",
            f["status"] == STATUS["Wait"] and abs(f["y"] - y0) < 1)
    # Freeze: the animation frame stops, then a hit lets it go.
    r.load("near", on(), percent=0)
    emu.h.call("resume")
    emu.advance(3)
    cpu_page(r, CPU_ROWS["freeze"])
    emu.h.call("pause")
    frames = emu.trace(20, emu.watch(lambda _: (round(r.g.extra(en)["motion"], 2), sname(r.g.fighter(en)["status"]))))
    emu.h.call("resume")
    still = len({m for m, _ in frames[2:]}) == 1
    res.row("Freeze CPU: animation frame and status still for 18 frames", "one value", frames[2:6], still)
    emu.hold(0, buttons=["A"])
    emu.advance(4)
    emu.neutral(0)
    emu.advance(40)
    log = emu.log_text()
    res.row("a hit lets the frozen CPU go", "unfrozen line", "yes" if "cpu: P2 unfrozen" in log else "no",
            "cpu: P2 unfrozen" in log)
    # Controlled By Port 1: the CPU moves with port 1's pad, and its
    # setup controller byte is put back afterwards.
    mode = emu.ptr(emu.ptr(nav.GAME_GLOBAL) + 0x8)
    byte = mode + nav.MELEE_PLAYERS + CPU * nav.MELEE_PLAYER_SIZE + 7
    r.load("start", on())
    before = emu.u8(byte)
    r.opt.set(controlledBy=1)
    # The first frame is still the AI's (Controlled By takes over from the
    # next), and what it starts there can last (Fox's blaster): let it end
    # with the pad neutral first.
    emu.h.call("resume")
    emu.advance(20)
    emu.h.call("pause")
    step_until(emu, "the CPU to stand", lambda: r.g.fighter(en)["status"] == STATUS["Wait"], 180)
    emu.hold(0, stick=(228, 128))
    frows = emu.trace(20, emu.watch(lambda _: r.g.fighter(en)))
    xs = [f["x"] for f in frows]
    seen = [(round(f["x"], 1), sname(f["status"]), round(f["lr"])) for f in frows]
    emu.neutral(0)
    during = emu.u8(byte)
    res.note(f"Controlled By: CPU (x, status, facing) {seen[::2]}")
    emu.h.call("resume")
    lab = LT.Lab(emu, [])
    lab.find_settings()
    cpu_page(r, CPU_ROWS["controlled by"], then=lambda lab_: lab_.tap(stick=LT.LEFT))
    emu.advance(5)
    after = emu.u8(byte)
    ran = any(s_ in ("Dash", "Run") for _, s_, _ in seen)
    res.row("Controlled By Port 1: the CPU runs right with port 1's stick", "dashes or runs, moves right",
            f"{'ran' if ran else 'no run'}, {xs[-1] - xs[0]:+.1f}", ran and xs[-1] - xs[0] > 5)
    res.row("its setup controller port: before / during / after None", f"{before} / 1 / {before}",
            f"{before} / {during} / {after}", during == 1 and after == before)
    return res


def o_controlled_by_load(r):
    """Controlled By = Port 1, quick save (D-pad right), Controlled By = None,
    quick load (D-pad left): the CPU's controller number and CPU type must
    return to baseline (not stay Port 1 / type 25) and P1's stick must not move
    the CPU."""
    res = Result("Controlled By across quick load")
    emu = r.emu
    r.load("start", on())
    emu.h.call("resume")
    emu.advance(10)

    mode = emu.ptr(emu.ptr(nav.GAME_GLOBAL) + 0x8)
    ctrl_byte = mode + nav.MELEE_PLAYERS + CPU * nav.MELEE_PLAYER_SIZE + 7
    p1 = r.g.entry(P1)
    en = r.g.entry(CPU)
    ai = r.g.inputs(en)[0]
    type_byte = ai + 0x113

    ctrl0, type0 = emu.u8(ctrl_byte), emu.u8(type_byte)
    res.note(f"before: controllerNo={ctrl0}, cpuType={type0}, aiFlag={r.g.ai_flag(1)}")

    # Set CPU Options > Controlled By = Port 1 through the lab menu.
    cpu_page(r, CPU_ROWS["controlled by"], then=lambda lab_: lab_.tap(stick=LT.RIGHT))
    if r.opt.get("controlledBy") != 1:
        raise CpuError(f"Controlled By is {r.opt.get('controlledBy')}, expected 1")
    emu.advance(20)
    ctrl_on, type_on = emu.u8(ctrl_byte), emu.u8(type_byte)
    res.note(f"Controlled By=Port 1: controllerNo={ctrl_on}, cpuType={type_on}, aiFlag={r.g.ai_flag(1)}")

    # Quick save (hold D-pad right) while Controlled By = Port 1.
    pos_qs = len(emu.log_text())
    emu.hold(0, ["DRIGHT"])
    emu.advance(14)
    emu.neutral(0)
    emu.wait_for("quick save", lambda: re.search(r"savestate: saved frame \d+", emu.log_text()[pos_qs:]),
                 120, every=2)
    emu.advance(35)  # clear 30f save lockout
    ctrl_saved, type_saved = emu.u8(ctrl_byte), emu.u8(type_byte)
    res.note(f"after quick save: controllerNo={ctrl_saved}, cpuType={type_saved}")

    # Set Controlled By = None through the lab menu.
    cpu_page(r, CPU_ROWS["controlled by"], then=lambda lab_: lab_.tap(stick=LT.LEFT))
    if r.opt.get("controlledBy") != 0:
        raise CpuError(f"Controlled By is {r.opt.get('controlledBy')}, expected 0")
    emu.advance(15)
    ctrl_none, type_none = emu.u8(ctrl_byte), emu.u8(type_byte)
    res.note(f"Controlled By=None: controllerNo={ctrl_none}, cpuType={type_none}, aiFlag={r.g.ai_flag(1)}")

    # Quick load (D-pad left).
    pos_ql = len(emu.log_text())
    emu.press(0, ["DLEFT"], frames=3, after=15)
    emu.wait_for("quick load", lambda: re.search(r"savestate: loaded frame \d+", emu.log_text()[pos_ql:]),
                 120, every=2)
    # Let any taunt from the D-pad right hold finish with pad neutral so the
    # 60-frame hold starts cleanly from Wait.
    emu.wait_for("CPU to stand", lambda: r.g.fighter(en)["status"] == STATUS["Wait"], 120, every=2)
    ctrl_loaded, type_loaded = emu.u8(ctrl_byte), emu.u8(type_byte)
    ai_loaded = r.g.ai_flag(1)
    res.note(f"after quick load (Controlled By={r.opt.get('controlledBy')}): controllerNo={ctrl_loaded}, "
             f"cpuType={type_loaded}, aiFlag={ai_loaded}")

    res.row("CPU controllerNo and cpuType after quick load with Controlled By=None",
            f"ctrl={ctrl0}, type={type0}",
            f"before ({ctrl0},{type0}) -> save ({ctrl_saved},{type_saved}) -> none ({ctrl_none},{type_none}) -> "
            f"load ({ctrl_loaded},{type_loaded})",
            ctrl_loaded == ctrl0 and type_loaded == type0)

    # Hold a direction on Port 1 for 60 frames and check whether the CPU moves.
    p1_x0 = r.g.fighter(p1)["x"]
    cpu_x0 = r.g.fighter(en)["x"]
    stick_dir = (28, 128) if cpu_x0 > 0 else (228, 128)
    dir_name = "LEFT" if stick_dir == (28, 128) else "RIGHT"
    emu.hold(0, stick=stick_dir)
    cpu_statuses = []
    for _ in range(60):
        emu.advance(1)
        cpu_statuses.append(sname(r.g.fighter(en)["status"]))
    emu.neutral(0)
    emu.advance(5)
    p1_x60 = r.g.fighter(p1)["x"]
    cpu_x60 = r.g.fighter(en)["x"]
    dx_p1 = p1_x60 - p1_x0
    dx_cpu = cpu_x60 - cpu_x0
    res.note(f"hold {dir_name} 60f: P1 x {p1_x0:.2f}->{p1_x60:.2f} (dx={dx_p1:+.2f}); "
             f"CPU x {cpu_x0:.2f}->{cpu_x60:.2f} (dx={dx_cpu:+.2f}), CPU statuses {sorted(set(cpu_statuses))}")
    res.row("60f stick hold on P1 with Controlled By=None: CPU must stay still",
            "CPU |dx| < 0.01, status Wait only",
            f"CPU dx={dx_cpu:+.2f}, statuses {sorted(set(cpu_statuses))}, P1 dx={dx_p1:+.2f}",
            abs(dx_cpu) < 0.01 and set(cpu_statuses) == {"Wait"} and abs(dx_p1) > 10.0)
    return res


def o_controlled_by_load_on(r):
    """Reverse direction of o_controlled_by_load: quick save with Controlled By
    = None, switch to Controlled By = Port 1, quick load -> the CPU's controller
    number and CPU type must be reconciled to (Port 1, type 25) immediately on
    the load (not left at baseline until a later frame), 60f of P1 stick input
    must move the CPU, and switching Controlled By back to None must restore
    baseline."""
    res = Result("Controlled By on across quick load")
    emu = r.emu
    r.load("start", on())
    emu.h.call("resume")
    emu.advance(10)

    mode = emu.ptr(emu.ptr(nav.GAME_GLOBAL) + 0x8)
    ctrl_byte = mode + nav.MELEE_PLAYERS + CPU * nav.MELEE_PLAYER_SIZE + 7
    p1 = r.g.entry(P1)
    en = r.g.entry(CPU)
    ai = r.g.inputs(en)[0]
    type_byte = ai + 0x113

    ctrl0, type0 = emu.u8(ctrl_byte), emu.u8(type_byte)
    res.note(f"before: controllerNo={ctrl0}, cpuType={type0}")

    # Quick save while Controlled By = None.
    pos_qs = len(emu.log_text())
    emu.hold(0, ["DRIGHT"])
    emu.advance(14)
    emu.neutral(0)
    emu.wait_for("quick save (CB=None)", lambda: re.search(r"savestate: saved frame \d+", emu.log_text()[pos_qs:]),
                 120, every=2)
    emu.advance(35)

    # Switch Controlled By = Port 1 through the lab menu.
    cpu_page(r, CPU_ROWS["controlled by"], then=lambda lab_: lab_.tap(stick=LT.RIGHT))
    if r.opt.get("controlledBy") != 1:
        raise CpuError(f"Controlled By is {r.opt.get('controlledBy')}, expected 1")
    emu.advance(15)
    ctrl_on, type_on = emu.u8(ctrl_byte), emu.u8(type_byte)
    res.note(f"Controlled By=Port 1: controllerNo={ctrl_on}, cpuType={type_on}")

    # Quick load (D-pad left) and inspect on the very frame the load completes.
    emu.h.call("pause")
    pos_ql = len(emu.log_text())
    emu.hold(0, ["DLEFT"])
    f0 = None
    for f in range(10):
        if f == 2:
            emu.neutral(0)
        emu.h.call("frame_advance", n=1)
        if f0 is None and re.search(r"savestate: loaded frame \d+", emu.log_text()[pos_ql:]):
            f0 = (emu.u8(ctrl_byte), emu.u8(type_byte))
    emu.neutral(0)
    emu.h.call("resume")
    emu.wait_for("CPU to stand", lambda: r.g.fighter(en)["status"] == STATUS["Wait"], 120, every=2)
    ctrl_loaded, type_loaded = emu.u8(ctrl_byte), emu.u8(type_byte)
    res.note(f"after quick load: f0={f0}, settled=({ctrl_loaded},{type_loaded})")
    res.row("immediate post-load (frame 0) controllerNo and cpuType reconciled to (1, 25)",
            "(1, 25)", f"f0={f0}, settled=({ctrl_loaded},{type_loaded})",
            f0 == (1, 25) and (ctrl_loaded, type_loaded) == (1, 25))

    # 60f stick hold on P1 must move CPU.
    cpu_x0 = r.g.fighter(en)["x"]
    stick_dir = (28, 128) if cpu_x0 > 0 else (228, 128)
    emu.hold(0, stick=stick_dir)
    for _ in range(60):
        emu.advance(1)
    emu.neutral(0)
    emu.advance(5)
    dx_cpu = r.g.fighter(en)["x"] - cpu_x0
    res.row("60f stick hold on P1 with Controlled By=Port 1 after load: CPU moves",
            "CPU |dx| > 10.0", f"CPU dx={dx_cpu:+.2f}", abs(dx_cpu) > 10.0)

    # Switch Controlled By back to None and check baseline restored.
    cpu_page(r, CPU_ROWS["controlled by"], then=lambda lab_: lab_.tap(stick=LT.LEFT))
    emu.advance(10)
    ctrl_end, type_end = emu.u8(ctrl_byte), emu.u8(type_byte)
    res.row("after Controlled By=None: controllerNo and cpuType back to baseline",
            f"({ctrl0}, {type0})", f"({ctrl_end}, {type_end})",
            (ctrl_end, type_end) == (ctrl0, type0))
    return res


def o_dash_through(r):
    """Counter Action (Ground) = Dash Through after one hit on Fox (FD):
    the CPU must dash through once and then be free (no retries, no repeated
    sprinting, and never running off the stage)."""
    res = Result("Counter Action (Ground) Dash Through")
    run_states = {STATUS["Dash"], STATUS["Turn_Dash"], STATUS["Run"], STATUS["Turn_Run"], 0x05, 0x09}

    # Case 1: spaced grounded hit (Mario throws a fireball ~45 units left of Fox,
    # then backs toward the left edge). Fox's initial dash ends before reaching
    # Mario, so a 15-frame hold enters Run (0x04) and triggers retries.
    r.load("start", on(counterGround=24), percent=0)
    pos = len(r.emu.log_text())
    shot = [None]
    hit = [None]

    def each(i, rows):
        if rows:
            p1x, cpux = rows[-1][P1]["x"], rows[-1][CPU]["x"]
            if shot[0] is None:
                if cpux - p1x > 45.0:
                    r.emu.hold(0, stick=(255, 128))
                else:
                    shot[0] = i
                    r.emu.hold(0, buttons=["B"], stick=(128, 128))
            elif i == shot[0] + 3:
                r.emu.neutral(0)
            elif i == shot[0] + 22:
                # Move back toward the left edge so an endlessly retrying CPU can reach it.
                r.emu.hold(0, stick=(0, 128))
            elif i == shot[0] + 45:
                r.emu.neutral(0)
            if hit[0] is None and len(rows) > 1:
                a, b = rows[-2][CPU], rows[-1][CPU]
                if b["hitlag"] > a["hitlag"] and b["hitstun"] > 0:
                    hit[0] = len(rows) - 1

    rows = r.record(620, players=(CPU, P1), each=each)
    r.emu.neutral(0)
    if hit[0] is None:
        raise CpuError(f"fireball did not hit Fox (shot={shot[0]}, statuses={statuses(rows)[:20]})")
    h = hit[0]
    log = r.emu.log_text()[pos:]
    initial = COUNTER_LOG.findall(log)
    retries = re.findall(r"cpu: P2 counter not taken \(status ([0-9a-fA-F]+)\), again, frame (\d+)", log)
    dash0 = first(rows, h + 1, lambda x: x[CPU]["status"] in (STATUS["Dash"], STATUS["Turn_Dash"]))
    run_frames = sum(1 for x in rows[dash0:] if x[CPU]["status"] in run_states) if dash0 is not None else 0
    settled = first(rows, dash0 + 1, lambda x: x[CPU]["status"] == STATUS["Wait"]) if dash0 is not None else None
    settled_after = settled - dash0 if settled is not None else None
    moved_toward = -moved(rows, dash0, frames=12) if dash0 is not None else 0.0
    xs = [x[CPU]["x"] for x in rows[h:]]
    min_x, max_x = min(xs), max(xs)
    left_stage = any(abs(x[CPU]["x"]) > FD_EDGE or x[CPU]["air"] for x in rows[dash0:]) if dash0 is not None else False
    ko = first(rows, h + 1, lambda x: x[CPU]["status"] in GONE)

    res.note(f"spaced hit at {h} (shot {shot[0]}), P1 x={rows[h][P1]['x']:.1f}, CPU x={rows[h][CPU]['x']:.1f}; dash0={dash0}")
    res.note(f"starts: {len(initial)} initial + {len(retries)} retries; retry statuses={[s for s, _ in retries[:10]]}")
    res.note(f"run/dash frames={run_frames}, settled_after={settled_after}, x=[{min_x:.1f}..{max_x:.1f}], "
             f"left_stage={left_stage}, ko={ko}")
    res.note(f"statuses after hit: {statuses(rows[h:])[:35]}")

    res.row("spaced hit: Dash Through starts once (no retries)",
            "1 start (0 retries), moved toward > 3",
            f"{len(initial)} initial + {len(retries)} retries, moved toward {moved_toward:+.1f}",
            len(initial) == 1 and len(retries) == 0 and dash0 is not None and moved_toward > 3)
    res.row("spaced hit: stops after one dash and stays on stage",
            "Wait <= 35f, run/dash <= 30f, on stage",
            f"Wait +{settled_after}f, run/dash {run_frames}f, x [{min_x:.1f}..{max_x:.1f}], "
            f"left_stage={left_stage}, ko={ko}",
            settled_after is not None and settled_after <= 35 and run_frames <= 30
            and not left_stage and ko is None)

    # Case 2: close grounded hit (ground_hit) must also do a single dash without dash-dancing back and forth.
    pos2 = len(r.emu.log_text())
    rows2, hit2 = ground_hit(r, 120, on(counterGround=24))
    log2 = r.emu.log_text()[pos2:]
    retries2 = re.findall(r"cpu: P2 counter not taken \(status ([0-9a-fA-F]+)\), again, frame (\d+)", log2)
    d2 = first(rows2, hit2 + 1, lambda x: x[CPU]["status"] in (STATUS["Dash"], STATUS["Turn_Dash"]))
    run2 = sum(1 for x in rows2[d2:] if x[CPU]["status"] in run_states) if d2 is not None else 0
    turns2 = sum(1 for k in range((d2 or 0) + 1, len(rows2))
                 if rows2[k][CPU]["status"] == STATUS["Turn_Dash"]
                 and rows2[k - 1][CPU]["status"] != STATUS["Turn_Dash"])
    res.note(f"close hit at {hit2}: dash0={d2}, run/dash={run2}f, turn_dashes={turns2}, retries={len(retries2)}, "
             f"statuses={statuses(rows2[hit2:])[:15]}")
    res.row("close hit: single dash through (no dash-dance or retries)",
            "0 retries, 0 Turn_Dash reversals, run/dash <= 25f",
            f"retries={len(retries2)}, turn_dashes={turns2}, run/dash={run2}f",
            d2 is not None and len(retries2) == 0 and turns2 == 0 and run2 <= 25)
    return res


def grab_run(r, opts, frames=260):
    """Mario grabs the CPU (Z) and holds it: (rows, first held row, release
    row, release status)."""
    r.load("near", opts, percent=0)

    def each(i, rows):
        if i == 0:
            r.emu.hold(0, buttons=["Z"])
        elif i == 4:
            r.emu.neutral(0)
    rows = r.record(frames, players=(CPU, P1), each=each, extras=True)
    held = first(rows, 0, lambda x: x[CPU]["status"] in (0x3D, 0x3E))
    rel = first(rows, held or 0, lambda x: x[CPU]["status"] in (0x40, 0x41)) if held is not None else None
    return rows, held, rel, (rows[rel][CPU]["status"] if rel is not None else None)


def o_grab(r):
    res = Result("Grab Escape and Release")
    dur = {}
    for name in ("None", "Medium", "High", "Perfect"):
        rows, held, rel, st = grab_run(r, on(grabEscape=MASH[name]))
        if held is None or rel is None:
            res.row(f"{name}: grabbed and released", "yes", f"held {held}, released {rel}", False)
            continue
        m0 = rows[held]["x"]["mash"]
        steps = [round(a["x"]["mash"] - b["x"]["mash"], 2) for a, b in zip(rows[held:rel], rows[held + 1:rel])]
        dur[name] = rel - held
        res.note(f"{name}: held {rel - held} frames (frames left {m0:.1f} at the grab), "
                 f"reductions {steps[:12]}")
        if name == "None":
            res.row("None: only the base reduction, 1 a frame", "all 1", sorted(set(steps)), set(steps) <= {1.0})
        if name == "Perfect":
            res.row("Perfect: a fresh button and stick every frame (8 each, P+)", "17 a frame",
                    sorted(set(steps)), steps and min(steps) >= 16.5)
        res.row(f"{name}: released on the ground", "Capture_Cut", sname(st), st == 0x40)
    if len(dur) == 4:
        # Medium and High mash on a random share of frames, so over a short
        # hold they can tie.
        res.row("hold time None > Medium >= High >= Perfect, None > Perfect", "yes",
                f"{dur['None']} / {dur['Medium']} / {dur['High']} / {dur['Perfect']}",
                dur["None"] > dur["Medium"] >= dur["High"] >= dur["Perfect"] and dur["None"] > dur["Perfect"])
    rows, held, rel, st = grab_run(r, on(grabEscape=MASH["High"], grabRelease=1))
    res.row("Airborne: released into Capture_Jump", "Capture_Jump", sname(st) if st else None, st == 0x41)
    # A counter after the release (the lab counts the release as a hit).
    rows, held, rel, st = grab_run(r, on(grabEscape=MASH["Perfect"], counterGround=13))
    k = first(rows, rel or 0, lambda x: x[CPU]["status"] == STATUS["Attack"])
    res.row("Perfect then Jab after the release", "Attack after Capture_Cut",
            f"{statuses(rows)[:6]}", rel is not None and k is not None)
    return res


def o_grab_airborne(r):
    """Grab Release: Airborne with Grab Escape Perfect (C9) must mash at full
    speed (17/frame: base 1 + fresh button 8 + stick clatter 8) while still
    holding Y above the Capture_Jump threshold (param 3141 = 0.635)."""
    res = Result("Grab Escape Airborne")
    for name in ("None", "Perfect"):
        rows, held, rel, st = grab_run(r, on(grabEscape=MASH[name], grabRelease=1))
        if held is None or rel is None:
            res.row(f"Airborne {name}: grabbed and released", "yes", f"held {held}, released {rel}", False)
            continue
        steps = [round(a["x"]["mash"] - b["x"]["mash"], 2) for a, b in zip(rows[held:rel], rows[held + 1:rel])]
        res.note(f"Airborne {name}: held {rel - held} frames, status {sname(st)}, reductions {steps[:12]}")
        res.row(f"Airborne {name}: released into Capture_Jump", "Capture_Jump",
                sname(st) if st else None, st == 0x41)
        if name == "None":
            res.row("Airborne None: only base reduction after initial hold-up", "1 a frame after frame 1",
                    sorted(set(steps[1:])), bool(steps[1:]) and set(steps[1:]) <= {1.0})
        else:
            res.row("Airborne Perfect: full button + stick mash (17 a frame)", "min >= 16.5",
                    sorted(set(steps)), bool(steps) and min(steps) >= 16.5)
    return res


ADV_ROW_HIT, ADV_ROW_LOGIC, ADV_ROW_GROUND = 0, 1, 2


def o_advanced(r):
    """Advanced Counter Options through the lab menu: hit 1 Custom with Up
    Smash (default ground counter Spotdodge), then hit 1 Disable; each
    exchange starts at hit 1 again (the count resets once the CPU is idle)."""
    res = Result("Advanced Counter")
    emu = r.emu
    g = ground_setup(r)
    crouch = g["kind"] == "crouch"
    hit_with = DTILT if crouch else JAB
    # The grounded hit of ground_setup (Behavior Crouch, ASDI Down for the
    # crouching one).
    held = {"behavior": BEHAVIOR["Crouch"], "asdi": ASDI["Down"]} if crouch else {}
    r.load("crouch" if crouch else "near", on(counterGround=1, **held))
    emu.h.call("resume")

    def to_custom_usmash(lab):
        lab.tap(["A"])  # open the page (cursor on the Advanced Counter row)
        emu.press(0, [], frames=30, after=4, stick=LT.UP)
        lab.down(ADV_ROW_LOGIC)
        lab.set_choice(SET_CPU + CPU_FIELDS["advLogic"], 2)
        lab.down(ADV_ROW_GROUND - ADV_ROW_LOGIC)
        lab.set_choice(SET_CPU + CPU_FIELDS["advGround"], 8)
        lab.back_to_main(1)
    cpu_page(r, CPU_ROWS["advanced"], then=to_custom_usmash)
    res.row("page fields: hit 1, Custom, Up Smash", "1 / 2 / 8",
            f"{r.opt.get('advHit')} / {r.opt.get('advLogic')} / {r.opt.get('advGround')}",
            (r.opt.get("advHit"), r.opt.get("advLogic"), r.opt.get("advGround")) == (1, 2, 8))
    # The hits live in the match module: save the state with them.
    emu.advance(20)
    emu.h.call("pause")
    r.states.save(emu, "adv-near")
    for k in range(2):
        rows, hit = r.hit_run("adv-near", hit_with, 90, dict({"control": LAB}, **held), percent=g["percent"],
                                hold=GROUND_JAB_HOLD)
        s = action_start(rows, hit)
        res.row(f"exchange {k + 1}, hit 1: Custom Up Smash (default Spotdodge)", "Attack_Hi4_Start",
                sname(rows[s][CPU]["status"]) if s else None,
                s is not None and rows[s][CPU]["status"] == STATUS["Attack_Hi4_Start"])

    # Random Advanced as the default: picks among the Custom hits' actions
    # (only hit 1's Up Smash here).
    rows, hit = r.hit_run("adv-near", hit_with, 90, dict({"control": LAB, "counterGround": 39}, **held),
                          percent=g["percent"], hold=GROUND_JAB_HOLD)
    s0 = action_start(rows, hit)
    res.row("default Random Advanced: one of the Custom hits' actions", "Attack_Hi4_Start",
            sname(rows[s0][CPU]["status"]) if s0 else None,
            s0 is not None and rows[s0][CPU]["status"] == STATUS["Attack_Hi4_Start"])

    def to_disable(lab):
        lab.tap(["A"])
        emu.press(0, [], frames=30, after=4, stick=LT.UP)
        lab.down(ADV_ROW_LOGIC)
        lab.set_choice(SET_CPU + CPU_FIELDS["advLogic"], 1)
        lab.back_to_main(1)
    r.load("adv-near", {"control": LAB})
    emu.h.call("resume")
    cpu_page(r, CPU_ROWS["advanced"], then=to_disable)
    emu.advance(20)
    emu.h.call("pause")
    r.states.save(emu, "adv-near")
    rows, hit = r.hit_run("adv-near", hit_with, 90, dict({"control": LAB}, **held), percent=g["percent"],
                                hold=GROUND_JAB_HOLD)
    s = action_start(rows, hit)
    res.row("hit 1 Disable: no counter", "no action", sname(rows[s][CPU]["status"]) if s else "none", s is None)
    return res


def o_tech_wait(r):
    """Miss Tech Wait Chance."""
    res = Result("Miss Tech Wait")
    # Miss Tech Wait 100%: Down_Wait lasts 15 frames longer before the get up.
    waits = {}
    for chance in (0, 100):
        rows, hit, land, s, _ = tech_run(r, on(tech=TECH["None"], getup=GETUP["Stand"], missTechWait=chance),
                                         frames=220)
        waits[chance] = sum(1 for x in rows[land:] if x[CPU]["status"] == 0x4D)
    # 100% rolls again each time a wait ends (the lab's), so the CPU lies
    # still until the game gets it up.
    res.row("Miss Tech Wait 0% / 100%: Down_Wait frames", "1-2 / 15 or more",
            f"{waits[0]} / {waits[100]}", waits[0] <= 2 and waits[100] >= 15)
    return res


# ---- a second hit on a tumbling CPU (Simulate Tech Trap, Tech Lockout) ----
#
# Fixture: P1 down-smashes the CPU (50%); after the hitlag the test holds the
# CPU, still tumbling, beside P1 a little above the floor (position and
# knockback written each frame) until P1 down-smashes again. After that
# second hit's hitlag the test writes the CPU's knockback speed so that it
# lands a chosen number of frames later. Positions come from the loaded
# state; the knockback is found per character by a short sweep. The tech
# is chosen once per hit, from the options then: a hold with no tech and
# the tech options from the second hit on.
HOLD_ABOVE = 4.0
# For a tech beside P1: high enough that no guard press falls in the hold.
HOLD_HIGH = 30.0


def kb_addr(r, en):
    """The knockback speed vector (kinetic module +0x14 -> +0x4C, +0x8)."""
    e = r.emu
    ft = en["fighters"][en["active"]]
    enum = e.u32(e.u32(ft + 0x60) + 0xD8)
    return e.u32(e.u32(e.u32(enum + 0x7C) + 0x14) + 0x4C) + 8


def put_cpu(r, en, x, y):
    post = r.g.modules(en["fighters"][en["active"]])["posture"]
    pos = struct.pack(">ff", x, y).hex()
    r.emu.h.call("write_mem", addr=post + 0xC, hex=pos)
    r.emu.h.call("write_mem", addr=post + 0x18, hex=pos)
    r.emu.h.call("write_mem", addr=kb_addr(r, en), hex=struct.pack(">ff", 0.0, 0.0).hex())


def rehit_run(r, before, after, kb, react=None, frames=200, after_tech=None):
    """The fixture above. `before`: options until the second hit (no tech,
    so that no guard press falls in the hold), `after`: from it. `kb`:
    (x away, y) knockback after the second hit's hitlag. With `after_tech`
    (frames), the hold ends early instead: the CPU lands and techs beside
    P1, whose second down smash is pressed `after_tech` frames after the
    tech starts. react(i, marks) may return script steps for the CPU (AI
    off). Returns (rows, marks): the rows of the hits, of the second
    hitlag's end, of the first tech and of the landing after it."""
    r.load("near", before, percent=50)
    en = r.g.entry(CPU)
    f0 = r.g.fighter(en)
    p1x = r.g.fighter(r.g.entry(P1))["x"]
    hx, hy = f0["x"], f0["y"] + (HOLD_ABOVE if after_tech is None else HOLD_HIGH)
    away = 1.0 if f0["x"] > p1x else -1.0
    if react is not None:
        r.drv.play(CPU, [NEUTRAL_STEP], loop=True, ai_off=True)
    m = {"hit1": None, "press2": None, "hit2": None, "free": None, "tech": None, "land": None}

    def each(i, rows):
        if i == 0:
            r.emu.hold(0, buttons=["A"], stick=DSMASH)
        if len(rows) >= 2:
            a, b = rows[-2][CPU], rows[-1][CPU]
            k = len(rows) - 1
            if b["hitlag"] > a["hitlag"]:
                if m["hit1"] is None:
                    m["hit1"] = k
                elif m["hit2"] is None and m["press2"] is not None:
                    m["hit2"] = k
                    r.opt.set(**after)
            if m["hit2"] is not None and m["free"] is None and b["hitlag"] == 0:
                m["free"] = k
                r.emu.h.call("write_mem", addr=kb_addr(r, en),
                             hex=struct.pack(">ff", kb[0] * away, kb[1]).hex())
            if m["tech"] is None and b["status"] in (STATUS["Passive"], STATUS["Passive_Fb"]):
                m["tech"] = k
            if m["free"] is not None and m["land"] is None and not b["air"] and b["status"] not in FLYING:
                m["land"] = k
            holding = m["hit1"] is not None and m["hit2"] is None and b["hitlag"] == 0
            if after_tech is not None and m["hit1"] is not None and (m["tech"] is not None or k > m["hit1"] + 40):
                holding = False
            if holding:
                put_cpu(r, en, hx, hy)
            p1 = rows[-1][P1]["status"]
            if m["press2"] is None and m["hit1"] is not None and k > m["hit1"] + 10 and p1 == STATUS["Wait"]:
                if after_tech is None or (m["tech"] is not None and k >= m["tech"] + after_tech):
                    m["press2"] = i
                    r.emu.hold(0, buttons=["A"], stick=DSMASH)
        if i == 5 or (m["press2"] is not None and i == m["press2"] + 5):
            r.emu.neutral(0)
        if react is not None:
            steps = react(i, m)
            if steps:
                r.drv.play(CPU, steps, ai_off=True)
    rows = r.record(frames, players=(CPU, P1), each=each)
    r.emu.neutral(0)
    return rows, m


def rehit_kb(r, want, after_tech=None, before=None):
    """The knockback (x, y) whose landing comes nearest `want` frames after
    the second hit, from a sweep: (kb, frames), or (None, None)."""
    best = (None, None)
    opts = before or on(tech=TECH["None"])
    for vy in (-1.0, -0.5, 0.0, 0.5, 1.0, 1.5, 2.0, 2.5, 3.0):
        rows, m = rehit_run(r, opts, opts, (0.6, vy), after_tech=after_tech)
        if m["land"] is None or m["free"] is None:
            continue
        got = m["land"] - m["hit2"]
        if best[1] is None or abs(got - want) < abs(best[1] - want):
            best = ((0.6, vy), got)
    return best


def o_tech_trap(r):
    """Simulate Tech Trap under the game's rule (a guard press techs only
    41 frames or more after the guard press before it). The second hit
    lands on the tumbling CPU; Off techs the landing; Latest presses guard
    in the hit's hitlag, so the game refuses a tech within 40 frames of it;
    Earliest makes no tech press before the frame a press 20 frames before
    the hit would allow (21 after it). The CPU scripted as a human who
    pressed guard as the hitlag ended and again just before landing gets
    Down from the game, as Latest does."""
    res = Result("Simulate Tech Trap")
    for want in (14, 32):
        kb, got = rehit_kb(r, want)
        if kb is None:
            res.row(f"knockback for a landing ~{want} frames after the second hit", "found", "none", False)
            continue
        res.note(f"landing {got} frames after the second hitlag with knockback {kb}")
        out = {}
        for trap in ("Off", "Earliest", "Latest"):
            rows, m = rehit_run(r, on(tech=TECH["None"]),
                                on(tech=TECH["In Place"], techTrap={"Off": 0, "Earliest": 1, "Latest": 2}[trap]), kb)
            prev = rows[m["hit2"] - 1][CPU]["status"] if m["hit2"] else None
            land = m["land"]
            out[trap] = sname(rows[land][CPU]["status"]) if land is not None else None
            res.note(f"{trap}: hit 1 row {m['hit1']}, hit 2 row {m['hit2']} (in {sname(prev) if prev else None}), "
                     f"hitlag over {m['free']}, landing {land}: {out[trap]}")

        def human(i, mm, early):
            if mm["free"] is None:
                return None
            if (early and i == mm["free"] + 1) or i == mm["hit2"] + got - 2:
                return [(GUARD, 0.0, 0.0, 1), (0, 0.0, 0.0, 300)]
            return None
        hum = {}
        for early in (True, False):
            rows, m = rehit_run(r, on(tech=TECH["None"]), on(tech=TECH["None"]), kb,
                                react=lambda i, mm, early=early: human(i, mm, early))
            hum[early] = sname(rows[m["land"]][CPU]["status"]) if m["land"] is not None else None
        want_s = ["Passive", "Passive" if got > 21 else "Down", "Down" if got <= 40 else "Passive"]
        res.row(f"landing {got} after the hit: Off / Earliest / Latest", " / ".join(want_s),
                " / ".join(str(out[t]) for t in ("Off", "Earliest", "Latest")),
                [out[t] for t in ("Off", "Earliest", "Latest")] == want_s)
        res.row(f"landing {got}: scripted guard as the hitlag ends and before landing / only before landing",
                "Down / Passive", f"{hum[True]} / {hum[False]}", (hum[True], hum[False]) == ("Down", "Passive"))
    return res


def o_tech_lockout(r):
    """Tech Lockout after a tech: the CPU techs in place beside P1, P1
    down-smashes it as the tech ends, and it lands again. The CPU never
    makes a second tech press less than 41 frames after the first (the
    game would refuse it), and the second tech comes out exactly when it
    made one. Earliest presses up to 15 frames before the predicted landing,
    Latest up to 5. The first tech uses Latest: the fixture holds the CPU
    high up, where a press 15 frames ahead would come during the hold."""
    res = Result("Tech Lockout")
    tech = None
    for after_tech in range(16, 34, 2):
        latest = on(tech=TECH["In Place"], techLockout=1)
        rows, m = rehit_run(r, latest, latest, (0.6, 0.0),
                            after_tech=after_tech)
        if m["hit2"] is not None:
            tech = after_tech
            res.note(f"P1's second down smash pressed {after_tech} frames into the tech hits (row {m['hit2']}, "
                     f"tech row {m['tech']})")
            break
    if tech is None:
        res.row("a down smash hits the CPU after its tech", "found", "none (16-32 frames tried)", False)
        return res
    for want in (4, 30):
        kb, got = rehit_kb(r, want, after_tech=tech, before=on(tech=TECH["In Place"], techLockout=1))
        if kb is None:
            res.row(f"knockback for a landing ~{want} frames after the hit", "found", "none", False)
            continue
        out = {}
        for mode in ("Earliest", "Latest"):
            pos = len(r.emu.log_text())
            rows, m = rehit_run(r, on(tech=TECH["In Place"], techLockout=1),
                                on(tech=TECH["In Place"], techLockout={"Earliest": 0, "Latest": 1}[mode]), kb,
                                after_tech=tech)
            frames = [int(f) for f in re.findall(r"cpu: P2 tech \d+: guard -?\d+ frames before the predicted "
                                                 r"landing, frame (\d+)", r.emu.log_text()[pos:])]
            gap = frames[1] - frames[0] if len(frames) >= 2 else None
            since = (m["land"] - m["tech"]) if m["land"] is not None and m["tech"] is not None else None
            out[mode] = (sname(rows[m["land"]][CPU]["status"]) if m["land"] is not None else None, since, gap)
            res.note(f"{mode}: tech row {m['tech']}, hit 2 row {m['hit2']}, landing {m['land']} "
                     f"({since} after the tech): {out[mode][0]}, second press {gap} frames after the first")
        for mode in ("Earliest", "Latest"):
            st, since, gap = out[mode]
            want = "Passive" if gap is not None else "Down"
            ok = st == want and (gap is None or gap >= 41)
            res.row(f"{mode}, landing {since} after the first tech: second press after / result",
                    "none or 41+ / Passive iff pressed", f"{gap} / {st}", ok)
    return res


def o_tech_damage_air(r):
    """Non-tumble Damage_Air (0x44) cannot tech: kHit must not call techInput()
    there, or the wasted guard press triggers P+'s 40-frame tech lockout
    (RSBE01.TXT:831-832) and locks out a follow-up tumble hit that lands within
    40 frames."""
    res = Result("No tech press in Damage_Air")
    r.load("near", on(tech=TECH["In Place"], techLockout=1), percent=0)
    en = r.g.entry(CPU)
    ai = r.g.inputs(en)[0]
    f0 = r.g.fighter(en)
    p1x = r.g.fighter(r.g.entry(P1))["x"]
    hx, hy = f0["x"], f0["y"]
    away = 1.0 if hx > p1x else -1.0
    pos = len(r.emu.log_text())
    held = []
    m = {"hit1": None, "press2": None, "hit2": None, "free2": None, "land2": None}

    def each(i, rows):
        held.append(r.g.pad(ai)["buttons"] & GUARD)
        if i == 0:
            r.emu.hold(0, buttons=["A"], stick=USMASH)
        elif i == 5:
            r.emu.neutral(0)
        if len(rows) >= 2:
            a, b = rows[-2][CPU], rows[-1][CPU]
            k = len(rows) - 1
            if b["hitlag"] > a["hitlag"]:
                if m["hit1"] is None:
                    m["hit1"] = k
                elif m["hit2"] is None and m["press2"] is not None:
                    m["hit2"] = k
            if m["hit2"] is not None and m["free2"] is None and b["hitlag"] == 0:
                m["free2"] = k
                put_fighter(r, en, hx, hy + 10.0)
                r.emu.h.call("write_mem", addr=kb_addr(r, en),
                             hex=struct.pack(">ff", 0.6 * away, -0.8).hex())
            if m["free2"] is not None and m["land2"] is None and not b["air"] and b["status"] not in FLYING:
                m["land2"] = k
            p1 = rows[-1][P1]["status"]
            if m["press2"] is None and m["hit1"] is not None and k > m["hit1"] + 10 and p1 == STATUS["Wait"]:
                r.opt.cpu_percent(50, True)
                put_fighter(r, en, hx, hy)
                r.emu.hold(0, buttons=["A"], stick=DSMASH)
                m["press2"] = i
        if m["press2"] is not None and i == m["press2"] + 5:
            r.emu.neutral(0)

    rows = r.record(130, players=(CPU, P1), each=each)
    r.emu.neutral(0)
    if m["hit1"] is None or m["hit2"] is None or m["free2"] is None or m["land2"] is None:
        raise CpuError(f"sequence incomplete: {m}, statuses {statuses(rows)[:12]}")
    rows_44 = [k for k in range(m["hit1"], m["hit2"])
               if rows[k][CPU]["status"] == STATUS["Damage_Air"] and rows[k][CPU]["air"]]
    # held[k + 1] is the input written in frame k (after row k - 1).
    guard_44 = [k for k in rows_44 if k + 1 < len(held) and held[k + 1]]
    tech_logs = re.findall(r"cpu: P2 tech [^\r\n]+", r.emu.log_text()[pos:])
    st_free2 = sname(rows[m["free2"]][CPU]["status"])
    st_land2 = sname(rows[m["land2"]][CPU]["status"])
    gap = m["land2"] - rows_44[-1] if rows_44 else None
    res.note(f"hit1 {m['hit1']}, Damage_Air rows {rows_44[0] if rows_44 else None}..{rows_44[-1] if rows_44 else None} "
             f"({len(rows_44)}f), guard during 0x44: {guard_44}; hit2 {m['hit2']} ({st_free2} at {m['free2']}), "
             f"land2 {m['land2']} ({st_land2}), gap from 0x44 end {gap}f; logs {tech_logs}")
    res.row("Damage_Air (0x44) above floor: guard presses during 0x44",
            "0", f"{len(guard_44)} ({guard_44[:4]})",
            len(rows_44) >= 6 and len(guard_44) == 0)
    res.row("follow-up tumble hit landing within 40f of 0x44: landing status",
            "Passive (gap <= 40)", f"{st_land2} (gap {gap}f)",
            rows[m["free2"]][CPU]["status"] in TUMBLE and gap is not None and gap <= 40 and st_land2 == "Passive")
    return res


def o_tech_edge(r):
    """Edge cases of CPU teching:
    1) A low spike landing within 3 frames after hitlag (Tech = In Place) must
       still be teched instead of blocked by kDiHold / hitlag.
    2) If the CPU presses guard for an expected landing and misses the surface
       (drifting past a platform), techPressed must clear after kTechGap so it
       can press again for the real landing.
    3) kTechFall must not call techInput once tumble has ended airborne (no
       midair airdodge)."""
    res = Result("Tech edge cases")
    en = r.g.entry(CPU)
    ai = r.g.inputs(en)[0]

    # 1) Low spike: Mario short-hop Fair (sweetspot meteor, frames 17-21,
    # Trajectory=280 in FitMario.pac 0x8A58) on genuinely airborne Fox low
    # above Final Destination so landing comes within 3f of hitlag end.
    r.load("near", on(tech=TECH["In Place"], techLockout=1), percent=50)
    held1 = []
    m1 = {"hit": None, "free": None, "land": None}

    def each1(i, rows):
        held1.append(r.g.pad(ai)["buttons"] & GUARD)
        if i == 0:
            r.emu.hold(0, buttons=["Y"])
        elif i == 1:
            r.emu.neutral(0)
        elif i == 5:
            r.emu.hold(0, buttons=["A"], stick=(228, 128))
        elif i == 8:
            r.emu.neutral(0)
        elif i == 20:
            # Fox enters Jump_Squat on 20-22 (below Fair's frame 15-16 60-deg
            # hitbox) and Jump on 23, meeting Fair's downward meteor hitbox
            # (frames 17-21, Trajectory=280) while low and airborne.
            r.opt.set(**on(behavior=BEHAVIOR["Jump"], tech=TECH["In Place"], techLockout=1))
        if len(rows) >= 2:
            a, b = rows[-2][CPU], rows[-1][CPU]
            k = len(rows) - 1
            if m1["hit"] is None and b["hitlag"] > a["hitlag"]:
                m1["hit"] = k
            if m1["hit"] is not None and m1["free"] is None and b["hitlag"] == 0:
                m1["free"] = k
            if m1["free"] is not None and m1["land"] is None and not b["air"] and b["status"] not in FLYING:
                m1["land"] = k

    rows1 = r.record(80, players=(CPU, P1), each=each1)
    r.emu.neutral(0)
    presses1 = [i - 1 for i in range(1, len(held1)) if held1[i] and not held1[i - 1]]
    st1 = sname(rows1[m1["land"]][CPU]["status"]) if m1["land"] is not None else None
    after1 = (m1["land"] - m1["free"]) if m1["land"] is not None and m1["free"] is not None else None
    hitlag1 = (m1["free"] - m1["hit"]) if m1["free"] is not None and m1["hit"] is not None else None
    y_hit1 = round(rows1[m1["hit"]][CPU]["y"], 1) if m1["hit"] is not None else None
    air_hit1 = rows1[m1["hit"]][CPU]["air"] if m1["hit"] is not None else None
    post1 = [(k, sname(rows1[k][CPU]["status"]), round(rows1[k][CPU]["y"], 1), rows1[k][CPU]["air"])
             for k in range(m1["free"] or 0, min(len(rows1), (m1["free"] or 0) + 8))] if m1["free"] else []
    res.note(f"low spike: hit {m1['hit']} (y {y_hit1}, air {air_hit1}, hitlag {hitlag1}f), free {m1['free']}, "
             f"land {m1['land']} (afterHitlag {after1}), status {st1}, guard presses {presses1}, post {post1}")
    res.row("low spike landing within 3f of hitlag: Tech In Place",
            "Passive (afterHitlag <= 3)", f"{st1} (afterHitlag {after1})",
            m1["land"] is not None and after1 is not None and after1 <= 3 and st1 == "Passive")

    # 2) Missed expected landing: after the first guard press in tumble, lift
    # the CPU high enough to fall > 41 frames before reaching the main floor.
    r.load("near", on(tech=TECH["In Place"], techLockout=1), percent=50)
    pos = len(r.emu.log_text())
    held2 = []
    m2 = {"hit": None, "press1": None, "lifted": False, "land": None}

    def each2(i, rows):
        held2.append(r.g.pad(ai)["buttons"] & GUARD)
        if i == 0:
            r.emu.hold(0, buttons=["A"], stick=DSMASH)
        elif i == 5:
            r.emu.neutral(0)
        if len(rows) >= 2:
            a, b = rows[-2][CPU], rows[-1][CPU]
            k = len(rows) - 1
            if m2["hit"] is None and b["hitlag"] > a["hitlag"]:
                m2["hit"] = k
            if m2["hit"] is not None and m2["press1"] is None and b["air"] and b["status"] in TUMBLE:
                if len(held2) >= 2 and held2[-1] and not held2[-2]:
                    m2["press1"] = k - 1
            if m2["press1"] is not None and not m2["lifted"] and k >= m2["press1"] + 1 and b["air"]:
                m2["lifted"] = True
                put_cpu(r, en, b["x"], b["y"] + 140.0)
            if m2["lifted"] and m2["land"] is None and not b["air"] and b["status"] not in FLYING:
                m2["land"] = k

    rows2 = r.record(160, players=(CPU, P1), each=each2)
    r.emu.neutral(0)
    presses2 = [i - 1 for i in range(1, len(held2)) if held2[i] and not held2[i - 1]]
    st2 = sname(rows2[m2["land"]][CPU]["status"]) if m2["land"] is not None else None
    gap2 = (presses2[1] - presses2[0]) if len(presses2) >= 2 else None
    fall2 = (m2["land"] - m2["press1"]) if m2["land"] is not None and m2["press1"] is not None else None
    res.note(f"missed platform: hit {m2['hit']}, press1 {m2['press1']}, lifted {m2['lifted']}, "
             f"land {m2['land']} ({fall2}f after press1), presses {presses2}, gap {gap2}, status {st2}")
    res.row("missed expected landing (>41f later): presses again and techs",
            "2+ presses (gap >= 41), Passive", f"presses {presses2} (gap {gap2}), {st2}",
            m2["lifted"] and m2["land"] is not None and len(presses2) >= 2 and gap2 is not None
            and gap2 >= 41 and st2 == "Passive")

    # 3) kTechFall airborne exit from tumble: enter kTechFall during Damage_Fly
    # heading into FD's right wall with a prior guard press -> Passive_Wall
    # (0x62) -> Fall (0x0E) while still airborne, then fall onto FD. kTechFall
    # must leave when tumble ends so techInput does not air dodge in Fall.
    r.load("near", on(control=TRAINING), percent=50)
    r.drv.play(CPU, [NEUTRAL_STEP], loop=True, ai_off=True)
    m3 = {"hit": None, "free": None, "lab": None, "wall": None, "fall": None}
    held3 = []

    def each3(i, rows):
        held3.append(r.g.pad(ai)["buttons"] & GUARD)
        if i == 0:
            r.emu.hold(0, buttons=["A"], stick=DSMASH)
        elif i == 5:
            r.emu.neutral(0)
        if len(rows) >= 2:
            a, b = rows[-2][CPU], rows[-1][CPU]
            k = len(rows) - 1
            if m3["hit"] is None and b["hitlag"] > a["hitlag"]:
                m3["hit"] = k
            if m3["hit"] is not None and m3["free"] is None and b["hitlag"] == 0:
                m3["free"] = k
                put_cpu(r, en, 92.0, -10.0)
                r.emu.h.call("write_mem", addr=kb_addr(r, en), hex=struct.pack(">ff", -3.0, 0.0).hex())
                r.drv.play(CPU, [(GUARD, 0.0, 0.0, 1), (0, 0.0, 0.0, 30)], ai_off=True)
            if m3["free"] is not None and m3["lab"] is None and k >= m3["free"] + 2:
                m3["lab"] = k
                r.drv.stop()
                r.opt.set(**on(tech=TECH["In Place"], techLockout=1, recovery=RECOVERY["Off"]))
            if m3["lab"] is not None and m3["wall"] is None and b["status"] == 0x62:
                m3["wall"] = k
            if m3["wall"] is not None and m3["fall"] is None and b["status"] == STATUS["Fall"]:
                m3["fall"] = k
                put_cpu(r, en, 0.0, 25.0)

    rows3 = r.record(130, players=(CPU, P1), each=each3)
    r.emu.neutral(0)
    r.drv.stop()
    presses3 = [i - 1 for i in range(1, len(held3)) if held3[i] and not held3[i - 1]]
    ad3 = first(rows3, m3["fall"] or 0, lambda x: x[CPU]["status"] == STATUS["Escape_Air"]) if m3["fall"] else None
    res.note(f"kTechFall wall->Fall: hit {m3['hit']}, free {m3['free']}, lab {m3['lab']}, wall {m3['wall']}, "
             f"fall {m3['fall']}, airdodge {ad3}, presses {presses3}, "
             f"statuses {statuses(rows3[m3['lab'] or 0:])[:12]}")
    res.row("kTechFall after Passive_Wall -> Fall: no midair airdodge",
            "wall-tech -> Fall, no Escape_Air", f"wall {m3['wall']}, fall {m3['fall']}, Escape_Air {ad3}",
            m3["wall"] is not None and m3["fall"] is not None and ad3 is None)
    return res


def o_wall_tech(r):
    """Wall tech behaviour (#13, NEW-1, C5):
    1) Recovering with Tech = None must not spoof guard trigger counters into
       floor or wall auto-techs.
    2) A stage-spike into FD's wall during hitstun (Tech = In Place, not
       previously recovering) must wall-tech (Passive_Wall / Passive_Wall_Jump).
    3) Recovering beside/below the ledge with Tech = In Place (and no Tech
       Trap) spoofs both trigger index 3 (Tech Window v0.10) and index 4
       (Fighter.pac); Simulate Tech Trap disables the spoof."""
    res = Result("Wall tech and trigger spoofing")
    en = r.g.entry(CPU)

    def read_triggers():
        ft = en["fighters"][en["active"]]
        acc = r.emu.u32(ft + 0x60)
        cm = r.emu.u32(r.emu.u32(acc + 0xD8) + 0x5C)
        b = r.emu.read(cm + 0x8E, 4)
        return (b[0], b[1]), (b[2], b[3])  # (idx3 prev,cnt), (idx4 prev,cnt)

    def start_recovery_then(opts, on_rec):
        r.load("near", opts, percent=50)
        f0 = r.g.fighter(en)
        hx, hy = f0["x"], f0["y"]
        m = {"hit1": None, "moved": False, "rec": None, "done": False}
        extra = {}

        def each(i, rows):
            if i == 0:
                r.emu.hold(0, buttons=["A"], stick=DSMASH)
            elif i == 5:
                r.emu.neutral(0)
            if len(rows) >= 2:
                a, b = rows[-2][CPU], rows[-1][CPU]
                k = len(rows) - 1
                if m["hit1"] is None and b["hitlag"] > a["hitlag"] and b["status"] in HIT:
                    m["hit1"] = k
                if m["hit1"] is not None and not m["moved"] and b["hitlag"] == 0:
                    m["moved"] = True
                    put_cpu(r, en, 95.0, 60.0)
                    r.emu.h.call("write_mem", addr=kb_addr(r, en), hex=struct.pack(">ff", 0.0, 1.0).hex())
                if m["moved"] and m["rec"] is None and r.g.ai_flag(en["slot"]) == 1:
                    m["rec"] = k
                if m["rec"] is not None:
                    on_rec(i, k, a, b, rows, hx, hy, m, extra)

        rows = r.record(190, players=(CPU, P1), each=each)
        r.emu.neutral(0)
        return rows, m, extra

    # 1a) C5: Tech = None while recovering -> hit onto FD floor within <19f
    # must NOT auto-tech (must land in Down, not Passive).
    def hit_to_floor(i, k, a, b, rows, hx, hy, m, ex):
        if "press2" not in ex and k >= m["rec"] + 2 and rows[-1][P1]["status"] == STATUS["Wait"]:
            r.emu.hold(0, buttons=["A"], stick=USMASH)
            ex["press2"] = i
        # Keep Fox recovering offstage until 2 frames before USMASH's active
        # frame so recovery's wallTech() spoof is still < 20 frames old at impact.
        if "press2" in ex and "hit2" not in ex and i >= ex["press2"] + 6:
            put_cpu(r, en, rows[-1][P1]["x"] + 6.0, 2.0)
        if "press2" in ex and i == ex["press2"] + 5:
            r.emu.neutral(0)
        if "press2" in ex and "hit2" not in ex and b["hitlag"] > a["hitlag"] and b["status"] in HIT:
            ex["hit2"] = k
        if "hit2" in ex and "free2" not in ex and b["hitlag"] == 0:
            ex["free2"] = k
            put_cpu(r, en, hx, hy + 4.0)
            r.emu.h.call("write_mem", addr=kb_addr(r, en), hex=struct.pack(">ff", 0.5, -2.5).hex())
        if "free2" in ex and "land2" not in ex and not b["air"] and b["status"] not in FLYING:
            ex["land2"] = k

    rows1a, m1a, ex1a = start_recovery_then(on(tech=TECH["None"], recovery=RECOVERY["Game"]), hit_to_floor)
    st1a = sname(rows1a[ex1a["land2"]][CPU]["status"]) if "land2" in ex1a else None
    dt1a = (ex1a["land2"] - ex1a["hit2"]) if "land2" in ex1a and "hit2" in ex1a else None
    res.note(f"C5 floor Tech=None: rec {m1a['rec']}, ex {ex1a}, dt {dt1a}f, status {st1a}")
    res.row("recovering with Tech=None then hit to floor: no auto-tech",
            "Down (land <= 19f after hit2)", f"{st1a} (+{dt1a}f)",
            m1a["rec"] is not None and "land2" in ex1a and dt1a is not None and dt1a <= 19 and st1a == "Down")

    # 1b) C5: Tech = None while recovering -> hit into FD wall within <19f
    # must NOT auto wall-tech (must bounce 0x47, not 0x62/0x63).
    def hit_to_wall(i, k, a, b, rows, hx, hy, m, ex):
        if "press2" not in ex and k >= m["rec"] + 2 and rows[-1][P1]["status"] == STATUS["Wait"]:
            r.emu.hold(0, buttons=["A"], stick=USMASH)
            ex["press2"] = i
        if "press2" in ex and "hit2" not in ex and i >= ex["press2"] + 6:
            put_cpu(r, en, rows[-1][P1]["x"] + 6.0, 2.0)
        if "press2" in ex and i == ex["press2"] + 5:
            r.emu.neutral(0)
        if "press2" in ex and "hit2" not in ex and b["hitlag"] > a["hitlag"] and b["status"] in HIT:
            ex["hit2"] = k
        if "hit2" in ex and "free2" not in ex and b["hitlag"] == 0:
            ex["free2"] = k
            put_cpu(r, en, 92.0, -10.0)
            r.emu.h.call("write_mem", addr=kb_addr(r, en), hex=struct.pack(">ff", -3.0, 0.0).hex())
        if "free2" in ex and "impact" not in ex and b["status"] in (0x47, 0x62, 0x63):
            ex["impact"] = (k, b["status"])

    rows1b, m1b, ex1b = start_recovery_then(on(tech=TECH["None"], recovery=RECOVERY["Game"]), hit_to_wall)
    imp1b = ex1b.get("impact")
    res.note(f"C5 wall Tech=None: rec {m1b['rec']}, ex {ex1b}, "
             f"statuses {statuses(rows1b[ex1b.get('free2', 0):])[:8]}")
    res.row("recovering with Tech=None then hit into wall: no auto wall-tech",
            "0x47 (Damage_Fly_Reflect_Lr)", f"{hex(imp1b[1]) if imp1b else None}",
            m1b["rec"] is not None and imp1b is not None and imp1b[1] == 0x47)

    # 2) NEW-1: Hitstun stage-spike into FD wall with Tech = In Place (not
    # previously recovering) must wall-tech (0x62 or 0x63), not bounce (0x47).
    r.load("near", on(tech=TECH["In Place"], techLockout=1, recovery=RECOVERY["Game"]), percent=50)
    m2 = {"hit": None, "free": None, "impact": None}

    def each2(i, rows):
        if i == 0:
            r.emu.hold(0, buttons=["A"], stick=DSMASH)
        elif i == 5:
            r.emu.neutral(0)
        if len(rows) >= 2:
            a, b = rows[-2][CPU], rows[-1][CPU]
            k = len(rows) - 1
            if m2["hit"] is None and b["hitlag"] > a["hitlag"] and b["status"] in HIT:
                m2["hit"] = k
            if m2["hit"] is not None and m2["free"] is None and b["hitlag"] == 0:
                m2["free"] = k
                put_cpu(r, en, 97.0, -10.0)
                r.emu.h.call("write_mem", addr=kb_addr(r, en), hex=struct.pack(">ff", -3.0, 0.15).hex())
            if m2["free"] is not None and m2["impact"] is None and b["status"] in (0x47, 0x62, 0x63):
                m2["impact"] = (k, b["status"])

    rows2 = r.record(80, players=(CPU, P1), each=each2)
    r.emu.neutral(0)
    imp2 = m2["impact"]
    res.note(f"NEW-1 hitstun wall tech: hit {m2['hit']}, free {m2['free']}, impact {imp2}, "
             f"statuses {statuses(rows2[m2['free'] or 0:])[:8]}")
    res.row("stage-spike into FD wall during hitstun (Tech=In Place): wall-techs",
            "0x62 or 0x63", f"{hex(imp2[1]) if imp2 else None}",
            imp2 is not None and imp2[1] in (0x62, 0x63))

    # 3) #13 & C5: Trigger indices while recovering below ledge: Tech = In Place
    # (no trap) spoofs both index 3 and index 4; Simulate Tech Trap does not.
    for label, trap, want_spoof in (("Tech=In Place, Trap=Off", 0, True),
                                    ("Tech=In Place, Trap=Latest", 2, False)):
        def sample_trig(i, k, a, b, rows, hx, hy, m, ex):
            if "trig" not in ex and k == m["rec"] + 4:
                ex["trig"] = read_triggers()

        _, m3, ex3 = start_recovery_then(on(tech=TECH["In Place"], techTrap=trap, recovery=RECOVERY["Game"]),
                                         sample_trig)
        trig = ex3.get("trig")
        i3, i4 = trig if trig else ((None, None), (None, None))
        spoofed3 = i3[0] is not None and i3[0] >= 40 and i3[1] < 20
        spoofed4 = i4[0] is not None and i4[0] >= 40 and i4[1] < 20
        res.note(f"#13/C5 {label}: rec {m3['rec']}, idx3 {i3}, idx4 {i4}")
        if want_spoof:
            res.row(f"recovering below ledge ({label}): both idx3 and idx4 spoofed",
                    "idx3 & idx4 prev>=40 cnt<20", f"idx3 {i3}, idx4 {i4}",
                    m3["rec"] is not None and spoofed3 and spoofed4)
        else:
            res.row(f"recovering below ledge ({label}): neither idx3 nor idx4 spoofed",
                    "neither spoofed (cnt>=20)", f"idx3 {i3}, idx4 {i4}",
                    m3["rec"] is not None and not spoofed3 and not spoofed4)
    return res


def o_tech_drift(r):
    """Pre-tech air drift telegraph: holding roll stick on every airborne frame
    after the Guard press drifts the CPU left/right in tumble (Damage_Fall) and
    telegraphs Tech Away / Tech Towards before landing. Roll stick must stay
    neutral until the landing window (at most 1 frame before landing) while all
    tech options still trigger reliably under both Earliest and Latest lockout."""
    res = Result("No pre-tech air drift telegraph")
    en = r.g.entry(CPU)
    ai = r.g.inputs(en)[0]
    # Use USMASH at 70% so Fox exits hitstun high in the air and falls through
    # Damage_Fall (where horizontal air drift is active) before landing.
    for mode, lockout in (("Earliest", 0), ("Latest", 1)):
        data = {}
        for name, want, sign_ in (("In Place", "Passive", 0), ("Away", "Passive_Fb", 1),
                                  ("Towards", "Passive_Fb", -1), ("None", "Down", 0)):
            sticks = []
            opts = on(tech=TECH[name], techLockout=lockout, tdi=TDI["None"], asdi=ASDI["Auto"],
                      sdiAmount=0, getup=GETUP["Stand"], recovery=RECOVERY["Off"])
            rows, hit = r.hit_run("near", USMASH, 160, opts, percent=70,
                                  react=lambda i, h, s=sticks: s.append(r.g.pad(ai)["x"]) or None)
            land = first(rows, hit + 1, lambda x: x[CPU]["status"] not in FLYING)
            if land is None:
                raise CpuError(f"tech-drift ({mode} {name}): CPU never landed")
            free = first(rows, hit + 1, lambda x: x[CPU]["hitlag"] == 0)
            dfall = sum(1 for k in range(hit + 1, land) if rows[k][CPU]["status"] == STATUS["Damage_Fall"])
            st = sname(rows[land][CPU]["status"])
            away = 1 if rows[land][CPU]["x"] > rows[land][P1]["x"] else -1
            moved = (rows[min(land + 25, len(rows) - 1)][CPU]["x"] - rows[land][CPU]["x"]) * away
            # sticks[k + 1] is the stick written on frame k. Check airborne frames
            # after hitlag ends up to 2 frames before landing (k <= land - 2).
            early_x = [(k, round(sticks[k + 1], 3)) for k in range((free or hit) + 1, max(land - 1, 0))
                       if k + 1 < len(sticks) and abs(sticks[k + 1]) > 0.01]
            data[name] = (st, moved, rows[land - 1][CPU]["x"], early_x, land, dfall)
            ok = st == want and (sign_ == 0 or moved * sign_ > 10)
            res.row(f"{mode} Tech {name}: landing status and roll",
                    f"{want}" + (", away" if sign_ > 0 else (", toward" if sign_ < 0 else "")),
                    f"{st}, {moved:+.1f} (dfall {dfall}f)", ok)
        x_ip = data["In Place"][2]
        for name in ("Away", "Towards"):
            st, moved, x_pre, early_x, land, dfall = data[name]
            dx = x_pre - x_ip
            res.note(f"{mode} Tech {name}: land {land} (dfall {dfall}f), x_pre {x_pre:.2f} (vs In Place {x_ip:.2f}, dx {dx:+.2f}), "
                     f"early non-zero stick frames {len(early_x)}: {early_x[:6]}")
            res.row(f"{mode} Tech {name}: no early roll stick (>= 2f before landing)",
                    "0 frames", f"{len(early_x)} frames ({early_x[:3]})", len(early_x) == 0)
            res.row(f"{mode} Tech {name}: pre-landing x matches Tech In Place",
                    "|dx| < 0.25 (dfall >= 10f)", f"dx={dx:+.2f} (dfall {dfall}f)",
                    dfall >= 10 and abs(dx) < 0.25)
    # Low spike with Tech Away (lands within 3f of hitlag): must still roll away.
    r.load("near", on(tech=TECH["Away"], techLockout=1, tdi=TDI["None"], recovery=RECOVERY["Off"]), percent=50)
    m = {"hit": None, "free": None, "land": None}

    def each_spike(i, rows):
        if i == 0:
            r.emu.hold(0, buttons=["Y"])
        elif i == 1:
            r.emu.neutral(0)
        elif i == 5:
            r.emu.hold(0, buttons=["A"], stick=(228, 128))
        elif i == 8:
            r.emu.neutral(0)
        elif i == 20:
            r.opt.set(**on(behavior=BEHAVIOR["Jump"], tech=TECH["Away"], techLockout=1, tdi=TDI["None"],
                           recovery=RECOVERY["Off"]))
        if len(rows) >= 2:
            a, b = rows[-2][CPU], rows[-1][CPU]
            k = len(rows) - 1
            if m["hit"] is None and b["hitlag"] > a["hitlag"]:
                m["hit"] = k
            if m["hit"] is not None and m["free"] is None and b["hitlag"] == 0:
                m["free"] = k
            if m["free"] is not None and m["land"] is None and not b["air"] and b["status"] not in FLYING:
                m["land"] = k

    rows_sp = r.record(95, players=(CPU, P1), each=each_spike)
    r.emu.neutral(0)
    land = m["land"]
    st_sp = sname(rows_sp[land][CPU]["status"]) if land is not None else None
    away_sp = 1 if (land is not None and rows_sp[land][CPU]["x"] > rows_sp[land][P1]["x"]) else -1
    moved_sp = (rows_sp[min(land + 25, len(rows_sp) - 1)][CPU]["x"] - rows_sp[land][CPU]["x"]) * away_sp if land else 0.0
    after_sp = (land - m["free"]) if land is not None and m["free"] is not None else None
    res.note(f"low spike Away: hit {m['hit']}, free {m['free']}, land {land} (after {after_sp}f), {st_sp}, moved {moved_sp:+.1f}")
    res.row("low spike (Tech Away): lands in Passive_Fb moving away",
            "Passive_Fb, > +10", f"{st_sp}, {moved_sp:+.1f} (after {after_sp}f)",
            land is not None and st_sp == "Passive_Fb" and moved_sp > 10)
    return res


def o_tech_slip(r):
    """Banana trip (Slip 0x89 / Slip_Down 0x8A) teching and Slip_Wait (0x8D)
    get-up options: P1 Diddy Kong pulls and throws a banana at Fox CPU."""
    res = Result("Banana trip tech and Slip_Wait get-up")
    SLIP_START = {STATUS["Slip"], STATUS["Slip_Down"]}
    SLIP_GETUPS = {STATUS["Slip_Stand"], STATUS["Slip_Stand_Attack"], STATUS["Slip_Stand_F"], STATUS["Slip_Stand_B"]}

    def banana_pos():
        sched = r.emu.ptr(nav.TASK_SCHEDULER)
        if not sched:
            return None
        task = r.emu.ptr(sched + 0x58 + 4 * 0xB)
        for _ in range(32):
            if not task:
                break
            vt = r.emu.ptr(task + 0x3C)
            if vt and r.emu.u32(vt + 0x10) == 0x8070F444:
                acc = r.emu.ptr(task + 0x60)
                en = r.emu.ptr(acc + 0xD8) if acc else None
                post = r.emu.ptr(en + 0xC) if en else None
                if post:
                    return r.emu.f32(post + 0xC), r.emu.f32(post + 0x10)
            task = r.emu.ptr(task + 0x10)
        return None

    # Setup once: put Diddy at -15 and Fox at +10, Diddy Down+B spawns a banana
    # peel, Diddy walks to the banana's x and picks it up with A (0x96), then
    # saves state "banana-held" so each trial just throws it right into Fox.
    r.load("far", on(tech=TECH["None"], recovery=RECOVERY["Off"]), percent=0)
    put_fighter(r, r.g.entry(P1), -15.0, 0.0)
    put_fighter(r, r.g.entry(CPU), 10.0, 0.0)
    picked = [False]
    last_item = [None]
    tap_at = [-10]

    def setup_each(i, rows):
        p1s = rows[-1][P1]["status"] if rows else STATUS["Wait"]
        p1x = rows[-1][P1]["x"] if rows else -15.0
        if p1s == 0x96:
            picked[0] = True
        if i == 0:
            r.emu.hold(0, buttons=["B"], stick=(128, 0))
        elif i == 4:
            r.emu.neutral(0)
        elif i >= 45 and not picked[0]:
            pos = banana_pos()
            if pos is not None:
                last_item[0] = pos
                bx, by = pos
                if by < 2.0 and abs(bx - p1x) < 4.5 and p1s in (STATUS["Wait"], STATUS["Walk"], STATUS["Walk_Brake"] if "Walk_Brake" in STATUS else 0x02):
                    if i >= tap_at[0] + 15:
                        tap_at[0] = i
                        r.emu.hold(0, buttons=["A"])
                    else:
                        r.emu.neutral(0)
                elif i >= tap_at[0] + 3:
                    r.emu.hold(0, stick=(188, 128) if bx > p1x else (68, 128))
            else:
                r.emu.neutral(0)
        elif picked[0]:
            r.emu.neutral(0)

    setup_rows = r.record(140, players=(CPU, P1), each=setup_each)
    r.emu.neutral(0)
    if not picked[0]:
        res.note(f"setup failed to pick up banana: item {last_item[0]}, P1 {statuses(setup_rows, P1)[:15]}, "
                 f"P1 x {setup_rows[0][P1]['x']:.1f} -> {setup_rows[-1][P1]['x']:.1f}")
        res.row("Diddy picks up banana peel", "Item_Pickup_Light (0x96)", "not seen", False)
        return res
    r.emu.h.call("pause")
    step_until(r.emu, "Diddy to hold banana in Wait",
               lambda: r.g.fighter(r.g.entry(P1))["status"] == STATUS["Wait"], 60)
    r.states.save(r.emu, "banana-held")
    r.emu.h.call("resume")

    def banana_run(options, frames=150):
        r.load("banana-held", options, percent=0)
        seen = {"slip": None}

        def each(i, rows):
            if i == 0:
                r.emu.hold(0, buttons=["A"], stick=(255, 128))
            elif i == 4:
                r.emu.neutral(0)
            if rows and seen["slip"] is None and rows[-1][CPU]["status"] in SLIP_START:
                seen["slip"] = len(rows) - 1

        rows = r.record(frames, players=(CPU, P1), each=each, extras=True)
        r.emu.neutral(0)
        return rows, seen["slip"]

    # 1) Tech options on banana trip: In Place, Away, Towards, None.
    for name, want, sign_ in (("In Place", "Passive", 0), ("Away", "Passive_Fb", 1),
                              ("Towards", "Passive_Fb", -1), ("None", "Slip_Wait", 0)):
        rows, slip = banana_run(on(tech=TECH[name], getup=GETUP["Stand"], recovery=RECOVERY["Off"]))
        if slip is None:
            res.note(f"Tech {name}: no slip triggered; P1 {statuses(rows, P1)[:10]}, CPU {statuses(rows)[:10]}")
            res.row(f"banana trip Tech {name}", want, "no slip", False)
            continue
        after = first(rows, slip + 1, lambda x: x[CPU]["status"] not in SLIP_START)
        st = sname(rows[after][CPU]["status"]) if after is not None else None
        away = 1 if (after is not None and rows[after][CPU]["x"] > rows[after][P1]["x"]) else -1
        moved = (rows[min(after + 25, len(rows) - 1)][CPU]["x"] - rows[after][CPU]["x"]) * away if after else 0.0
        mf_end = rows[after - 1]["x"]["motion"] if after is not None and after > 0 else None
        res.note(f"Tech {name}: slip {slip} ({sname(rows[slip][CPU]['status'])}), next {after} ({st}, moved {moved:+.1f}, "
                 f"last slip mframe {mf_end}); post {statuses(rows[slip:])[:8]}")
        ok = st == want and (sign_ == 0 or moved * sign_ > 10)
        res.row(f"banana trip Tech {name}",
                f"{want}" + (", away" if sign_ > 0 else (", toward" if sign_ < 0 else "")),
                f"{st}, {moved:+.1f}", ok)

    # 2) Missed banana tech -> Slip_Wait (0x8D) get-up options.
    for name, want_set, sign_ in (("Stand", {"Slip_Stand"}, 0),
                                  ("Attack", {"Slip_Stand_Attack"}, 0),
                                  ("Away", {"Slip_Stand_F", "Slip_Stand_B"}, 1),
                                  ("Towards", {"Slip_Stand_F", "Slip_Stand_B"}, -1)):
        rows, slip = banana_run(on(tech=TECH["None"], getup=GETUP[name], missTechWait=0, recovery=RECOVERY["Off"]),
                                frames=230)
        wait = first(rows, slip or 0, lambda x: x[CPU]["status"] == STATUS["Slip_Wait"]) if slip is not None else None
        g = first(rows, wait, lambda x: x[CPU]["status"] in SLIP_GETUPS) if wait is not None else None
        got = sname(rows[g][CPU]["status"]) if g is not None else None
        moved = 0.0
        if g is not None:
            away = 1 if rows[g][CPU]["x"] > rows[g][P1]["x"] else -1
            moved = (rows[min(g + 25, len(rows) - 1)][CPU]["x"] - rows[g][CPU]["x"]) * away
        waited = (g - wait) if (wait is not None and g is not None) else None
        res.note(f"Slip_Wait Get Up {name}: slip {slip}, wait {wait}, getup {g} ({got}, +{waited}f, moved {moved:+.1f}); "
                 f"post {statuses(rows[slip or 0:])[:8]}")
        ok = (wait is not None and g is not None and waited is not None and waited <= 10
              and got in want_set and (sign_ == 0 or moved * sign_ > 5))
        res.row(f"Slip_Wait Get Up {name}",
                f"{'/'.join(sorted(want_set))} <= +10f" + (", away" if sign_ > 0 else (", toward" if sign_ < 0 else "")),
                f"{got} (+{waited}f, {moved:+.1f})", ok)

    # 3) Miss Tech Wait 100% in Slip_Wait: must wait >= 15 frames before getup.
    waits = {}
    for chance in (0, 100):
        rows, slip = banana_run(on(tech=TECH["None"], getup=GETUP["Stand"], missTechWait=chance,
                                   recovery=RECOVERY["Off"]), frames=240)
        waits[chance] = sum(1 for x in rows[slip or 0:] if x[CPU]["status"] == STATUS["Slip_Wait"]) if slip is not None else 0
    res.row("Slip_Wait Miss Tech Wait 0% / 100%: Slip_Wait frames", "1-3 / 15 or more",
            f"{waits[0]} / {waits[100]}", 1 <= waits[0] <= 3 and waits[100] >= 15)
    return res


def o_tech_stage(r):
    """On Bowser's Castle (stairs and platforms): the CPU, moved onto
    Mario's platform with Move CPU, is down-smashed off it and lands lower,
    on the stairs or the floor; Tech In Place must still tech there."""
    res = Result("tech on platforms and slopes")
    emu = r.emu
    r.drv.stop()
    r.opt.set(**on())
    en, p1 = r.g.entry(CPU), r.g.entry(P1)
    emu.advance(30)
    px, cx = r.g.fighter(p1)["x"], r.g.fighter(en)["x"]
    py = r.g.fighter(p1)["y"]
    cpu_page(r, CPU_ROWS["move"])
    target = px + (9.0 if cx > px else -9.0)
    emu.hold(0, stick=(28, 128) if cx > target else (228, 128))
    emu.wait_for("the CPU over Mario's platform", lambda: abs(r.g.fighter(en)["x"] - target) < 1.5, 600)
    emu.neutral(0)
    emu.hold(0, stick=(128, 228))
    emu.wait_for("the CPU above it", lambda: r.g.fighter(en)["y"] > py + 6, 120)
    emu.neutral(0)
    cpu_page(r, CPU_ROWS["move"])
    emu.advance(90)
    f = r.g.fighter(en)
    res.note(f"P1 at ({px:.1f}, {py:.1f}); CPU moved to ({f['x']:.1f}, {f['y']:.1f}) {sname(f['status'])}")
    res.row("Move CPU put the CPU on Mario's platform", "standing at Mario's height",
            f"{sname(f['status'])}, y {f['y']:.1f}", f["status"] == STATUS["Wait"] and abs(f["y"] - py) < 1)
    # Saved with the lab's control off, so that the percent lock, which
    # finds the CPU by its AI flag, applies when the state is loaded.
    r.opt.set(control=TRAINING)
    emu.advance(5)
    emu.h.call("pause")
    r.states.save(emu, "castle")
    # A percent that sends this character off the platform without a KO.
    pct = 60
    for trial in (120, 90, 60, 40, 25, 15):
        r.load("castle", on(tech=TECH["None"], getup=GETUP["Stand"]), percent=trial)
        emu.hold(0, buttons=["A"], stick=FSMASH if f["x"] > px else (0, 128))
        trows = r.record(200, players=(CPU, P1), each=lambda i, rows: emu.neutral(0) if i == 5 else None)
        landed = first(trows, 16, lambda x: not x[CPU]["air"] and x[CPU]["status"] not in GONE)
        if landed is not None and trows[landed][CPU]["y"] < py - 5 and not any(x[CPU]["status"] in GONE
                                                                              for x in trows):
            pct = trial
            break
    res.note(f"forward smash at {pct}%")
    for tech, want in (("In Place", "Passive"), ("Away", "Passive_Fb"), ("None", "Down")):
        r.load("castle", on(tech=TECH[tech], getup=GETUP["Stand"]), percent=pct)
        emu.hold(0, buttons=["A"], stick=FSMASH if f["x"] > px else (0, 128))

        ai = r.g.inputs(r.g.entry(CPU))[0]
        held = []

        def each(i, rows):
            held.append(r.g.pad(ai)["buttons"] & GUARD)
            if i == 5:
                emu.neutral(0)
        rows = r.record(200, players=(CPU, P1), each=each)
        # held[i] is the input written in the frame of row i - 1.
        presses = [i - 1 for i in range(1, len(held)) if held[i] and not held[i - 1]]
        res.note(f"{tech}: P1 {statuses(rows, P1)[:4]}, CPU {statuses(rows)[:6]}, guard presses on rows {presses}, "
                 f"CPU damage {rows[2][CPU]['damage']:.0f} -> {rows[-1][CPU]['damage']:.0f}")
        hit = first(rows, 0, lambda x: x[CPU]["hitlag"] > 0)
        land = first(rows, (hit or 0) + 1, lambda x: not x[CPU]["air"]) if hit is not None else None
        ly = rows[land][CPU]["y"] if land else None
        res.row(f"Tech {tech}: lands below the platform it stood on", f"{want}, y < {py - 5:.0f}",
                f"{sname(rows[land][CPU]['status']) if land else None}, y {ly}",
                land is not None and sname(rows[land][CPU]["status"]) == want and ly < py - 5)
    return res


def m_oos_probe(r):
    """From standing: the down special pressed on each frame of the jump
    squat (A); a double jump with an aerial after it, the jump held or let go
    (B); the earliest double jump after a jump (C); jump and down held for
    a float (D). Statuses, height and airborne rows; it checks nothing."""
    res = Result("out of shield probes")

    def trial(label, steps, frames=45):
        r.load("near")
        r.drv.play(CPU, steps + [(0, 0.0, 0.0, 200)], ai_off=True)
        rows = r.record(frames)
        y0 = rows[0][CPU]["y"]
        res.note(f"{label}: {statuses(rows)}")
        res.note(f"   y: {[round(q[CPU]['y'] - y0, 2) for q in rows]}")
        res.note(f"   air: {''.join('a' if q[CPU]['air'] else '.' for q in rows)}")
        return rows

    for k in range(4):
        trial(f"A jump, down special {k + 1} frames later", [(JUMP, 0.0, 0.0, 1)] + ([(0, 0.0, 0.0, k)] if k else [])
              + [(B, 0.0, -1.0, 1)], 30)
    trial("A guard, then down special with guard held", [(GUARD, 0.0, 0.0, 12), (GUARD | B, 0.0, -1.0, 1),
                                                          (GUARD, 0.0, 0.0, 10)], 30)
    sh = [(JUMP, 0.0, 0.0, 1), (0, 0.0, 0.0, 9)]
    trial("B short hop, double jump", sh + [(JUMP, 0.0, 0.0, 1)], 55)
    trial("B double jump, neutral air next frame, jump let go", sh + [(JUMP, 0.0, 0.0, 1), (A, 0.0, 0.0, 1)], 55)
    trial("B double jump, neutral air next frame, jump held", sh + [(JUMP, 0.0, 0.0, 1), (JUMP | A, 0.0, 0.0, 1),
                                                                   (JUMP, 0.0, 0.0, 15)], 55)
    trial("B double jump, neutral air 3 frames later, jump let go", sh + [(JUMP, 0.0, 0.0, 1), (0, 0.0, 0.0, 2),
                                                                         (A, 0.0, 0.0, 1)], 55)
    trial("B double jump, neutral air 3 frames later, jump held", sh + [(JUMP, 0.0, 0.0, 3), (JUMP | A, 0.0, 0.0, 1),
                                                                       (JUMP, 0.0, 0.0, 15)], 55)
    for k in (2, 3, 4, 5, 6, 7, 8):
        trial(f"C jump, jump again {k} frames later", [(JUMP, 0.0, 0.0, 1), (0, 0.0, 0.0, k - 1), (JUMP, 0.0, 0.0, 1)],
              20)
    trial("D jump held", [(JUMP, 0.0, 0.0, 70)], 70)
    trial("D jump with down held", [(JUMP, 0.0, -1.0, 70)], 70)
    trial("D jump, down held", [(JUMP, 0.0, 0.0, 1), (0, 0.0, -1.0, 70)], 70)
    trial("D jump, down held, neutral air", [(JUMP, 0.0, -1.0, 1), (0, 0.0, -1.0, 8), (A, 0.0, 0.0, 1),
                                            (0, 0.0, -1.0, 40)], 70)
    for k in (4, 5, 6):
        trial(f"E jump and down held, jump + A on row {k + 2}", [(JUMP, 0.0, 0.0, 1), (JUMP, 0.0, -1.0, k),
                                                                (JUMP | A, 0.0, 0.0, 1), (JUMP, 0.0, -1.0, 20)], 30)
    fl = [(JUMP, 0.0, 0.0, 1), (JUMP, 0.0, -1.0, 7)]
    for label, step in (("jump + A", (JUMP | A, 0.0, 0.0, 1)), ("A", (A, 0.0, 0.0, 1)),
                        ("jump + A, down", (JUMP | A, 0.0, -1.0, 1)), ("A, down", (A, 0.0, -1.0, 1))):
        trial(f"E float, then {label}", fl + [step, (JUMP, 0.0, -1.0, 20)], 40)
        trial(f"E float, then {label} 2 later", fl + [(JUMP, 0.0, -1.0, 2), step, (JUMP, 0.0, -1.0, 20)], 40)
    return res


def o_throw_laydown(r):
    """Behavior Shield after Snake's down throw (0x42 Capture_Thrown -> 0x50
    Lay_Down, non-tumble): getupInput() must act on Lay_Down (0x50, same getup
    terms as Down_Wait in common.md) so the CPU gets up, runs any ground
    counter, and shields again instead of lying flat in Lay_Down."""
    res = Result("Behavior Shield after Lay_Down throw (Snake)")
    DTHROW = (128, 0)
    for label, extra, want_getup in (
        ("default (Stand, None)", {"getup": GETUP["Stand"], "counterGround": 0}, STATUS["Down_Stand"]),
        ("Roll Away + Spotdodge", {"getup": GETUP["Away"], "counterGround": 1}, STATUS["Down_Stand_Fb"]),
    ):
        opts = on(behavior=BEHAVIOR["Shield"], recovery=RECOVERY["Game"], **extra)
        r.load("near", opts, percent=0)
        seen = {}

        def each(i, rows):
            if i == 25:
                r.emu.hold(0, buttons=["Z"])
            elif i == 29:
                r.emu.neutral(0)
            c = rows[-1][CPU] if rows else None
            if c is not None and "held" not in seen and c["status"] in (0x3D, 0x3E):
                seen["held"] = i
                r.emu.hold(0, stick=DTHROW)
            elif "held" in seen and i == seen["held"] + 4:
                r.emu.neutral(0)

        rows = r.record(200, players=(CPU, P1), each=each)
        r.emu.neutral(0)
        thrown = first(rows, 0, lambda x: x[CPU]["status"] == 0x42)
        lay = first(rows, thrown or 0, lambda x: x[CPU]["status"] == 0x50) if thrown is not None else None
        got_up = first(rows, lay or 0, lambda x: x[CPU]["status"] == want_getup) if lay is not None else None
        guard = first(rows, got_up or (lay or 0), lambda x: x[CPU]["status"] in (STATUS["Guard_On"], STATUS["Guard"])) \
            if lay is not None else None
        res.note(f"{label}: thrown {thrown}, Lay_Down {lay}, getup({sname(want_getup)}) {got_up}, "
                 f"guard {guard}; post {statuses(rows[thrown or 0:])[:10]}")
        ok = (lay is not None and got_up is not None and got_up <= lay + 25
              and guard is not None and guard <= lay + 100)
        res.row(f"{label}: gets up from Lay_Down and shields again",
                f"Lay_Down -> {sname(want_getup)} <= +25f -> Guard <= +100f",
                f"lay {lay}, getup {got_up}, guard {guard}", ok)
    return res


def o_tech_ray_edge(r):
    """Downward ground ray in framesToLand() must cast straight down (0, -400)
    rather than along (nx, b - 400): standing on Battlefield's right platform
    (groundY ~ 27.2) and falling onto the main floor near its outer right edge
    (x = 68.1, floor edge ~ 68.4) with downward speed must hit the main floor
    at y ~ 0 and tech cleanly."""
    res = Result("Tech ray direction near floor edge")
    emu = r.emu
    r.drv.stop()
    r.opt.set(control=TRAINING)
    en, p1 = r.g.entry(CPU), r.g.entry(P1)
    ai = r.g.inputs(en)[0]
    emu.advance(40)
    cf0 = r.g.fighter(en)
    plat_y = cf0["y"]
    # Land Mario on the right side platform beside Fox so Fox's recorded
    # groundY stays at the platform height (~27.2) when hit.
    land_at(r, p1, cf0["x"] - 8.5, plat_y + 10.0)
    emu.h.call("pause")
    put_fighter(r, p1, cf0["x"] - 8.5, plat_y)
    put_fighter(r, en, cf0["x"], plat_y)
    r.drv.play(CPU, [NEUTRAL_STEP], loop=True)
    emu.step(frames=10)
    r.states.save(emu, "bf-plat")
    emu.h.call("resume")
    res.note(f"Battlefield right platform y={plat_y:.2f}, CPU start x={cf0['x']:.2f}")

    def fall_to_floor(x_pos, lockout, y0=24.5, kb_y=-5.0):
        r.load("bf-plat", on(tech=TECH["In Place"], techLockout=lockout, recovery=RECOVERY["Off"]), percent=50)
        held = []
        m = {"hit": None, "free": None, "land": None}

        def each(i, rows):
            held.append(r.g.pad(ai)["buttons"] & GUARD)
            if i == 0:
                emu.hold(0, buttons=["A"], stick=DSMASH)
            elif i == 5:
                emu.neutral(0)
            if len(rows) >= 2:
                a, b = rows[-2][CPU], rows[-1][CPU]
                k = len(rows) - 1
                if m["hit"] is None and b["hitlag"] > a["hitlag"]:
                    m["hit"] = k
                if m["hit"] is not None and m["free"] is None and b["hitlag"] == 0:
                    m["free"] = k
                    put_cpu(r, en, x_pos, y0)
                    emu.h.call("write_mem", addr=kb_addr(r, en), hex=struct.pack(">ff", 0.0, kb_y).hex())
                if m["free"] is not None and m["land"] is None and not b["air"] and b["status"] not in FLYING:
                    m["land"] = k

        rows = r.record(90, players=(CPU, P1), each=each)
        emu.neutral(0)
        presses = [i - 1 for i in range((m["free"] or 0) + 1, len(held)) if held[i] and not held[i - 1]]
        land = m["land"]
        st = sname(rows[land][CPU]["status"]) if land is not None else None
        ly = rows[land][CPU]["y"] if land is not None else None
        lead = (land - presses[0]) if (land is not None and presses) else None
        return m, st, ly, lead, presses

    # 1) Interior main floor (x=0.0) control: lands on main floor and techs.
    m1, st1, ly1, lead1, pr1 = fall_to_floor(0.0, 1, y0=24.5, kb_y=-5.0)
    res.note(f"x=0.0 control: hit {m1['hit']}, free {m1['free']}, land {m1['land']} (y={ly1}), "
             f"status {st1}, lead {lead1}, presses {pr1}")
    res.row("main floor interior (x=0.0, Latest): techs on main floor",
            "Passive at y~0.0, lead 2..9",
            f"{st1} at y={ly1}, lead {lead1}",
            ly1 is not None and abs(ly1) < 1.5 and st1 == "Passive"
            and lead1 is not None and 2 <= lead1 <= 9)

    # 2) Near Battlefield's main floor outer right edge (x=68.1 vs edge 68.4):
    # with groundY still at the platform (~27.2), a straight-down ray (0, -400)
    # hits the floor at y ~ 0 and techs cleanly.
    m2, st2, ly2, lead2, pr2 = fall_to_floor(68.1, 1, y0=24.5, kb_y=-5.0)
    res.note(f"x=68.1 near edge: hit {m2['hit']}, free {m2['free']}, land {m2['land']} (y={ly2}), "
             f"status {st2}, lead {lead2}, presses {pr2}")
    res.row("main floor near edge (x=68.1, Latest): techs on main floor",
            "Passive at y~0.0, lead 2..9",
            f"{st2} at y={ly2}, lead {lead2}",
            ly2 is not None and abs(ly2) < 1.5 and st2 == "Passive"
            and lead2 is not None and 2 <= lead2 <= 9)
    return res


def o_air_jump_fly(r):
    """Multi-jumper Fly (0x0D) recognition in options.cpp:
    1) started() must recognize kStatusFly (0x0D) so Jump Neutral does not log
       'counter not taken' and burn a second midair jump.
    2) reached(kUntilJumped) must recognize kStatusFly (0x0D) so Jump Away
       applies away drift on the first Fly frame instead of waiting 2 frames."""
    res = Result("Multi-jumper Fly (0x0D) air jump counter")
    en = r.g.entry(CPU)
    ai = r.g.inputs(en)[0]

    for idx, label in ((4, "Jump Neutral"), (2, "Jump Away")):
        pos = len(r.emu.log_text())
        jheld, sticks = [], []

        def react(i, h, jh=jheld, st=sticks):
            pad = r.g.pad(ai)
            jh.append(pad["buttons"] & JUMP)
            st.append(pad["x"])
            return None

        rows, hit = r.hit_run("near", USMASH, 140, on(counterAir=idx, recovery=RECOVERY["Off"]),
                              percent=50, react=react)
        fly = first(rows, hit + 1, lambda x: x[CPU]["status"] == 0x0D)
        retries = re.findall(r"cpu: P2 counter not taken[^\r\n]*", r.emu.log_text()[pos:])
        presses = [i - 1 for i in range(hit + 1, len(jheld)) if jheld[i] and not jheld[i - 1]]
        res.note(f"{label}: hit {hit}, fly {fly}, jump presses {presses}, retries {retries}, "
                 f"post {statuses(rows[fly:])[:6] if fly is not None else None}")
        res.row(f"{label}: enters Fly (0x0D) once with no retry",
                "Fly, 1 jump press, 0 retries",
                f"fly {fly}, {len(presses)} press(es) {presses}, {len(retries)} retries",
                fly is not None and len(presses) == 1 and len(retries) == 0)
        if idx == 2 and fly is not None:
            away = 1.0 if rows[fly][CPU]["x"] >= rows[fly][P1]["x"] else -1.0
            # Frame `fly` is the JUMP press (sticks[fly + 1] == 0); frame
            # `fly + 1` is the first frame options.cpp sees Fly (0x0D), where
            # reached(kUntilJumped) advances immediately to away drift
            # (sticks[fly + 2]) instead of waiting 2 frames until fly + 3.
            sx_after_fly = sticks[fly + 2] if fly + 2 < len(sticks) else 0.0
            res.note(f"Jump Away stick around Fly ({fly}): "
                     f"{[(k, round(sticks[k + 1], 2)) for k in range(max(0, fly - 1), min(len(sticks) - 1, fly + 5))]}")
            res.row("Jump Away: away drift starts immediately after Fly (0x0D) entry",
                    f"stick_x * away > 0.5 at frame {fly + 1}",
                    f"stick_x={sx_after_fly:+.2f} (away={away:+.0f})",
                    sx_after_fly * away > 0.5)
    return res


def o_auto_lcancel(r):
    """CPU Auto L-Cancel (default On, toggleable Off):
    1) SHFFL Dair with Auto L-Cancel On sets LA-Basic[90] > 0 before landing
       and halves Landing_Attack_Air (0x18) frames vs Auto L-Cancel Off.
    2) Out-of-shield SHFFL Dair with Auto L-Cancel On also halves landing lag.
    3) Full Hop empty fall with Auto L-Cancel On injects zero Guard presses and
       never triggers Escape_Air (0x21) or Air_Lasso (0x7F)."""
    res = Result("CPU Auto L-Cancel")
    en = r.g.entry(CPU)
    ai = r.g.inputs(en)[0]

    def run_shffl(auto_lc, action=GROUND["SHFFL Dair"]):
        r.load("start", on(autoLCancel=auto_lc, shfflTiming=SHFFL_TIMING["Frame-Perfect"]), percent=0)
        guards = []

        def each(i, rows):
            if i == 2:
                r.cue.op(CueBlock.CUE, 0, action, 0)
            guards.append(bool(r.g.pad(ai)["buttons"] & GUARD))

        rows = r.record(95, players=(CPU, P1), each=each, extras=True)
        atk = first(rows, 2, lambda x: x[CPU]["status"] == STATUS["Attack_Air"])
        land = first(rows, atk or 2, lambda x: not x[CPU]["air"]) if atk is not None else None
        air_lc = [x["x"]["lcancel"] for x in rows[atk:land] if x[CPU]["status"] == STATUS["Attack_Air"]] if (
            atk is not None and land is not None) else []
        lag = sum(1 for x in rows[land:] if x[CPU]["status"] == STATUS["Landing_Attack_Air"]) if land is not None else 0
        bad = [sname(x[CPU]["status"]) for x in rows
               if x[CPU]["status"] in (STATUS["Escape_Air"], STATUS["Air_Lasso"])]
        return rows, atk, land, max(air_lc) if air_lc else -999, lag, bad, guards

    _, atk_on, land_on, lc_on, lag_on, bad_on, _ = run_shffl(1)
    _, atk_off, land_off, lc_off, lag_off, bad_off, _ = run_shffl(0)
    res.note(f"SHFFL Dair On: atk {atk_on}, land {land_on}, max LA-Basic[90]={lc_on}, lag={lag_on}f, bad={bad_on}")
    res.note(f"SHFFL Dair Off: atk {atk_off}, land {land_off}, max LA-Basic[90]={lc_off}, lag={lag_off}f, bad={bad_off}")
    res.row("SHFFL Dair Auto L-Cancel On sets LA-Basic[90] and halves lag",
            f"lc > 0, lag <= {lag_off // 2 + 1}, no Escape_Air/Air_Lasso",
            f"lc={lc_on}, lag {lag_on}f vs {lag_off}f, bad={bad_on}",
            lc_on > 0 and lag_on > 0 and lag_off >= 12 and lag_on <= lag_off // 2 + 1
            and lag_on < lag_off - 4 and not bad_on)
    res.row("SHFFL Dair Auto L-Cancel Off leaves LA-Basic[90] <= 0 and full lag",
            "lc <= 0, full lag, no Escape_Air/Air_Lasso",
            f"lc={lc_off}, lag={lag_off}f, bad={bad_off}",
            lc_off <= 0 and lag_off >= 12 and not bad_off)

    # Out-of-shield SHFFL Dair: On halves Landing_Attack_Air lag vs Off.
    oos_lags, oos_lc = {}, {}
    for auto_lc in (1, 0):
        rows, stun, end = shield_run(r, on(behavior=BEHAVIOR["Shield"], counterShield=SHIELD_ACTIONS["SHFFL Dair"],
                                           autoLCancel=auto_lc, shfflTiming=SHFFL_TIMING["Frame-Perfect"]))
        atk = first(rows, end or 0, lambda x: x[CPU]["status"] == STATUS["Attack_Air"]) if end is not None else None
        land = first(rows, atk or 0, lambda x: not x[CPU]["air"]) if atk is not None else None
        lc_vals = [x["x"]["lcancel"] for x in rows[atk:land] if x[CPU]["status"] == STATUS["Attack_Air"]] if (
            atk is not None and land is not None) else []
        oos_lc[auto_lc] = max(lc_vals) if lc_vals else -999
        oos_lags[auto_lc] = (sum(1 for x in rows[land:] if x[CPU]["status"] == STATUS["Landing_Attack_Air"])
                             if land is not None else 0)
    res.note(f"OoS SHFFL Dair: On lc={oos_lc[1]} lag={oos_lags[1]}f vs Off lc={oos_lc[0]} lag={oos_lags[0]}f")
    res.row("OoS SHFFL Dair: Auto L-Cancel On halves landing lag vs Off",
            f"lc > 0, lag_on <= {oos_lags[0] // 2 + 1}",
            f"lc={oos_lc[1]}/{oos_lc[0]}, lag={oos_lags[1]}f/{oos_lags[0]}f",
            oos_lc[1] > 0 and oos_lc[0] <= 0 and oos_lags[1] > 0 and oos_lags[0] >= 12
            and oos_lags[1] <= oos_lags[0] // 2 + 1)

    # Empty Full Hop with Auto L-Cancel On: never presses Guard or airdodges in Fall.
    r.load("start", on(autoLCancel=1), percent=0)
    fh_guards = []

    def fh_each(i, rows):
        if i == 2:
            r.cue.op(CueBlock.CUE, 0, GROUND["Full Hop"], 0)
        fh_guards.append(bool(r.g.pad(ai)["buttons"] & GUARD))

    fh_rows = r.record(80, players=(CPU, P1), each=fh_each, extras=True)
    fh_air = any(x[CPU]["air"] and x[CPU]["status"] in (STATUS["Jump"], STATUS["Fall"]) for x in fh_rows)
    fh_landed = any(not x[CPU]["air"] for x in fh_rows[15:])
    fh_bad = [sname(x[CPU]["status"]) for x in fh_rows
              if x[CPU]["status"] in (STATUS["Escape_Air"], STATUS["Air_Lasso"])]
    fh_guard_cnt = sum(1 for g in fh_guards[2:] if g)
    res.row("Full Hop empty fall with Auto L-Cancel On: zero Guard / Escape_Air",
            "air arc + landing, 0 Guard, 0 Escape_Air/Air_Lasso",
            f"air={fh_air}, landed={fh_landed}, guards={fh_guard_cnt}, bad={fh_bad}",
            fh_air and fh_landed and fh_guard_cnt == 0 and not fh_bad)
    return res


def o_shffl_timing(r):
    """SHFFL Timing presets (Frame-Perfect / Early / Normal / Late / Random):
    verifies exact frame-offset ordering from takeoff, real fastfall vy jump,
    clean Landing_Attack_Air with Auto L-Cancel, and clean Wait recovery."""
    res = Result("SHFFL Timing presets")

    def run_preset(timing_name, action=GROUND["SHFFL Dair"]):
        r.load("start", on(autoLCancel=1, shfflTiming=SHFFL_TIMING[timing_name]), percent=0)

        def each(i, rows):
            if i == 2:
                r.cue.op(CueBlock.CUE, 0, action, 0)

        rows = r.record(95, players=(CPU, P1), each=each, extras=True)
        sq = first(rows, 2, lambda x: x[CPU]["status"] == STATUS["Jump_Squat"])
        takeoff = first(rows, sq or 2, lambda x: x[CPU]["air"]) if sq is not None else None
        atk = first(rows, takeoff or 2, lambda x: x[CPU]["status"] == STATUS["Attack_Air"]) if takeoff is not None else None
        land = first(rows, takeoff or 2, lambda x: not x[CPU]["air"]) if takeoff is not None else None
        vys = [rows[k][CPU]["y"] - rows[k - 1][CPU]["y"] for k in range(1, len(rows))]
        ff = None
        if takeoff is not None and land is not None:
            for k in range(takeoff + 1, land):
                if vys[k - 1] - vys[k - 2] < -0.3:
                    ff = k
                    break
        land_st = sname(rows[land][CPU]["status"]) if land is not None else None
        lc_vals = [x["x"]["lcancel"] for x in rows[atk:land] if x[CPU]["status"] == STATUS["Attack_Air"]] if (
            atk is not None and land is not None) else []
        wait_back = first(rows, land + 1, lambda x: x[CPU]["status"] == STATUS["Wait"]) if land is not None else None
        post_bad = ([sname(x[CPU]["status"]) for x in rows[wait_back:]
                     if x[CPU]["status"] in (STATUS["Jump_Squat"], STATUS["Squat"], STATUS["Squat_Wait"])]
                    if wait_back is not None else ["no_wait"])
        return {
            "takeoff": takeoff, "atk": atk, "atk_off": (atk - takeoff) if (atk is not None and takeoff is not None) else None,
            "ff": ff, "ff_off": (ff - takeoff) if (ff is not None and takeoff is not None) else None,
            "land": land, "land_st": land_st, "max_lc": max(lc_vals) if lc_vals else -999,
            "wait_back": wait_back, "post_bad": post_bad
        }

    m = {}
    for name in ("Frame-Perfect", "Early", "Normal", "Late", "Random"):
        m[name] = run_preset(name)
        d = m[name]
        res.note(f"{name:<13}: takeoff={d['takeoff']}, atk=+{d['atk_off']}, ff=+{d['ff_off']}, "
                 f"land={d['land']} ({d['land_st']}), lc={d['max_lc']}, wait={d['wait_back']}, post_bad={d['post_bad']}")

    fp, ea, no, la, ra = m["Frame-Perfect"], m["Early"], m["Normal"], m["Late"], m["Random"]
    res.row("Frame-Perfect SHFFL: frame-1 aerial (+0), fastfall, L-cancelled landing",
            "atk +0, ff present, Landing_Attack_Air, lc > 0, clean Wait",
            f"atk +{fp['atk_off']}, ff +{fp['ff_off']}, {fp['land_st']}, lc={fp['max_lc']}, post={fp['post_bad']}",
            fp["atk_off"] == 0 and fp["ff"] is not None and fp["land_st"] == "Landing_Attack_Air"
            and fp["max_lc"] > 0 and not fp["post_bad"])
    res.row("Early & Normal SHFFL: monotonic aerial delays (0 < Early < Normal) + fastfall",
            "Early +2, Normal +4, both fastfall and land cleanly",
            f"Early +{ea['atk_off']} (ff +{ea['ff_off']}), Normal +{no['atk_off']} (ff +{no['ff_off']})",
            ea["atk_off"] == 2 and no["atk_off"] == 4 and ea["ff"] is not None and no["ff"] is not None
            and ea["land_st"] == "Landing_Attack_Air" and no["land_st"] == "Landing_Attack_Air"
            and not ea["post_bad"] and not no["post_bad"])
    res.row("Late SHFFL: apex late aerial (+late > Normal), immediate fastfall & L-cancel",
            "atk > +4, ff right after atk, Landing_Attack_Air, lc > 0, clean Wait",
            f"atk +{la['atk_off']}, ff +{la['ff_off']}, {la['land_st']}, lc={la['max_lc']}, post={la['post_bad']}",
            la["atk_off"] is not None and la["atk_off"] > 4 and la["ff"] is not None
            and la["ff"] >= la["atk"] and la["ff"] <= la["atk"] + 2
            and la["land_st"] == "Landing_Attack_Air" and la["max_lc"] > 0 and not la["post_bad"])
    res.row("Random SHFFL: executes valid Attack_Air + fastfall + clean Wait",
            "Attack_Air, fastfall, Landing_Attack_Air, clean Wait",
            f"atk +{ra['atk_off']}, ff +{ra['ff_off']}, {ra['land_st']}, post={ra['post_bad']}",
            ra["atk_off"] is not None and ra["ff"] is not None and ra["land_st"] == "Landing_Attack_Air"
            and not ra["post_bad"])
    return res


def o_ffl_air(r):
    """Air counter FFL (Fastfall L-Cancel aerial): Mario's Up Smash at 50%
    launches Fox straight up onstage so hitstun ends in air above FD.
    Verifies Attack_Air + real fastfall + L-cancelled landing to Wait, with
    Late starting strictly later than Frame-Perfect."""
    res = Result("Air counter FFL (Frame-Perfect vs Late)")

    out = {}
    for name in ("Frame-Perfect", "Late"):
        rows, h = r.hit_run("near", USMASH, 180,
                            on(counterAir=AIR_ACTIONS["FFL Dair"], autoLCancel=1,
                               shfflTiming=SHFFL_TIMING[name]),
                            percent=50, extras=True)
        end = first(rows, h + 1, lambda x: x[CPU]["hitstun"] == 0 and x[CPU]["hitlag"] == 0)
        atk = first(rows, h + 1, lambda x: x[CPU]["status"] == STATUS["Attack_Air"])
        land = first(rows, atk or (h + 1), lambda x: not x[CPU]["air"]) if atk is not None else None
        vys = [rows[k][CPU]["y"] - rows[k - 1][CPU]["y"] for k in range(1, len(rows))]
        ff = None
        if atk is not None and land is not None:
            for k in range(atk + 1, land):
                if vys[k - 1] - vys[k - 2] < -0.3:
                    ff = k
                    break
        lc_vals = [x["x"]["lcancel"] for x in rows[atk:land] if x[CPU]["status"] == STATUS["Attack_Air"]] if (
            atk is not None and land is not None) else []
        wait_back = first(rows, land + 1, lambda x: x[CPU]["status"] == STATUS["Wait"]) if land is not None else None
        bad = [sname(x[CPU]["status"]) for x in rows[h:]
               if x[CPU]["status"] in (STATUS["Escape_Air"], STATUS["Air_Lasso"])]
        out[name] = {
            "hit": h, "end": end, "atk": atk, "atk_off": (atk - h) if atk is not None else None,
            "ff": ff, "land": land, "land_st": sname(rows[land][CPU]["status"]) if land is not None else None,
            "max_lc": max(lc_vals) if lc_vals else -999,
            "wait_back": wait_back, "bad": bad
        }
        d = out[name]
        res.note(f"{name:<13}: hit={h}, end={end}, atk=+{d['atk_off']}, ff={ff}, "
                 f"land={land} ({d['land_st']}), lc={d['max_lc']}, wait={wait_back}, bad={bad}")

    fp, la = out["Frame-Perfect"], out["Late"]
    res.row("FFL Dair Frame-Perfect: Attack_Air, fastfall, L-cancelled landing to Wait",
            "Attack_Air, fastfall, Landing_Attack_Air, lc > 0, Wait",
            f"atk +{fp['atk_off']}, ff={fp['ff']}, land={fp['land_st']}, lc={fp['max_lc']}, wait={fp['wait_back']}",
            fp["atk"] is not None and fp["ff"] is not None and fp["land_st"] == "Landing_Attack_Air"
            and fp["max_lc"] > 0 and fp["wait_back"] is not None and not fp["bad"])
    res.row("FFL Dair Late: starts strictly later than Frame-Perfect + fastfall + Wait",
            f"atk > +{fp['atk_off']}, fastfall, Landing_Attack_Air, lc > 0, Wait",
            f"atk +{la['atk_off']}, ff={la['ff']}, land={la['land_st']}, lc={la['max_lc']}, wait={la['wait_back']}",
            la["atk_off"] is not None and fp["atk_off"] is not None and la["atk_off"] > fp["atk_off"]
            and la["ff"] is not None and la["land_st"] == "Landing_Attack_Air"
            and la["max_lc"] > 0 and la["wait_back"] is not None and not la["bad"])
    return res


def o_auto_lcancel_tether(r):
    """Tether character (Link) safety check: Auto L-Cancel On halves
    Landing_Attack_Air lag on SHFFL Dair with zero accidental Air_Lasso (0x7F)
    or Escape_Air (0x21), and Full Hop empty fall emits zero Guard / Zair."""
    res = Result("Auto L-Cancel tether safety (Link)")
    emu = r.emu
    r.drv.play(CPU, [NEUTRAL_STEP], loop=True)
    set_training_option(emu, "Attack")
    emu.h.call("pause")
    emu.step(frames=20)
    r.states.save(emu, "tether-start")
    emu.h.call("resume")

    en = r.g.entry(CPU)
    ai = r.g.inputs(en)[0]

    def run_link(auto_lc, action):
        r.load("tether-start", on(autoLCancel=auto_lc, shfflTiming=SHFFL_TIMING["Frame-Perfect"]), percent=0)
        guards = []

        def each(i, rows):
            if i == 2:
                r.cue.op(CueBlock.CUE, 0, action, 0)
            guards.append(bool(r.g.pad(ai)["buttons"] & GUARD))

        rows = r.record(110, players=(CPU, P1), each=each, extras=True)
        atk = first(rows, 2, lambda x: x[CPU]["status"] == STATUS["Attack_Air"])
        land = first(rows, atk or 2, lambda x: not x[CPU]["air"]) if atk is not None else None
        lc_vals = [x["x"]["lcancel"] for x in rows[atk:land] if x[CPU]["status"] == STATUS["Attack_Air"]] if (
            atk is not None and land is not None) else []
        lag = sum(1 for x in rows[land:] if x[CPU]["status"] == STATUS["Landing_Attack_Air"]) if land is not None else 0
        bad = [sname(x[CPU]["status"]) for x in rows
               if x[CPU]["status"] in (STATUS["Escape_Air"], STATUS["Air_Lasso"])]
        return rows, atk, land, max(lc_vals) if lc_vals else -999, lag, bad, sum(1 for g in guards[2:] if g)

    _, atk_on, land_on, lc_on, lag_on, bad_on, _ = run_link(1, GROUND["SHFFL Dair"])
    _, atk_off, land_off, lc_off, lag_off, bad_off, _ = run_link(0, GROUND["SHFFL Dair"])
    fh_rows, _, _, _, _, fh_bad, fh_guards = run_link(1, GROUND["Full Hop"])
    fh_fall = any(x[CPU]["status"] == STATUS["Fall"] for x in fh_rows)
    res.note(f"Link SHFFL Dair On: atk={atk_on}, land={land_on}, lc={lc_on}, lag={lag_on}f, bad={bad_on}")
    res.note(f"Link SHFFL Dair Off: atk={atk_off}, land={land_off}, lc={lc_off}, lag={lag_off}f, bad={bad_off}")
    res.row("Link SHFFL Dair Auto L-Cancel On halves lag with zero Zair/Airdodge",
            f"lc > 0, lag <= {lag_off // 2 + 1}, 0 Air_Lasso/Escape_Air",
            f"lc={lc_on}, lag {lag_on}f vs {lag_off}f, bad={bad_on}",
            lc_on > 0 and lc_off <= 0 and lag_on > 0 and lag_off >= 14
            and lag_on <= lag_off // 2 + 1 and lag_on < lag_off - 5 and not bad_on and not bad_off)
    res.row("Link Full Hop empty fall with Auto L-Cancel On: zero Guard / Zair / Airdodge",
            "Fall seen, 0 Guard, 0 Air_Lasso/Escape_Air",
            f"Fall={fh_fall}, guards={fh_guards}, bad={fh_bad}",
            fh_fall and fh_guards == 0 and not fh_bad)
    return res


MEASURE = [("oos-probe", m_oos_probe)]

SCENARIOS = [("c1", c1_shield), ("c2", c2_jump), ("c3", c3_sequence), ("c4", c4_stick), ("c5", c5_silence),
             ("e", e_human), ("di", d_di), ("tech", d_tech),
             ("shield", o_shield), ("counter-ground", o_counter_ground), ("counter-delay-land", o_counter_delay_land),
             ("counter-delay-tumble", o_counter_delay_tumble), ("counter-shield", o_counter_shield),
             ("shield-slideoff", o_shield_slideoff), ("shield-oos-retry", o_shield_oos_retry),
             ("counter-air", o_counter_air), ("tdi", o_tdi), ("sdi", o_sdi), ("dual-stick-sdi", o_dual_stick_sdi),
             ("own-hitlag", o_own_hitlag), ("techopts", o_tech),
             ("techrandom", o_tech_random), ("tech-invis", o_tech_invis), ("silence", o_silence),
             ("intangible", o_intangible),
             ("menu", o_menu), ("training", o_training), ("multi", o_multi),
             ("actions-ground", o_actions_ground), ("actions-air", o_actions_air),
             ("actions-shield", o_actions_shield), ("shield-angle", o_shield_angle), ("powershield", o_powershield),
             ("powershield-cancel", o_powershield_cancel),
             ("asdi", o_asdi), ("asdi-probe", o_asdi_probe), ("tdi-measured", o_tdi_measured),
             ("tdi-angles", o_tdi_angles), ("cue", o_cue),
             ("landing", o_landing_light), ("shield-end", o_shield_end), ("wiggle", o_wiggle),
             ("air-jump", o_air_jump), ("air-jump-empty", o_air_jump_empty),
             ("move-freeze", o_move_freeze),
             ("controlled-by-load", o_controlled_by_load),
             ("controlled-by-load-on", o_controlled_by_load_on),
             ("dash-through", o_dash_through),
             ("grab", o_grab), ("grab-airborne", o_grab_airborne),
             ("advanced", o_advanced), ("tech-wait", o_tech_wait), ("tech-trap", o_tech_trap),
             ("tech-lockout", o_tech_lockout), ("tech-damage-air", o_tech_damage_air), ("tech-edge", o_tech_edge),
             ("tech-drift", o_tech_drift),
             ("wall-tech", o_wall_tech), ("percent-lock", o_percent_lock),
             ("throw-behavior", o_throw_behavior), ("counter-tumble", o_counter_tumble),
             ("after-tech-counter", o_after_tech_counter),
             ("auto-lcancel", o_auto_lcancel), ("shffl-timing", o_shffl_timing), ("ffl-air", o_ffl_air),
             ("oos-techniques", o_oos_techniques), ("finish", o_finish)]
# Run in a second match, against an Ice Climbers CPU.
NANA = ("nana", o_nana)
# Run in a match on Bowser's Castle (stairs and platforms), position 8 of
# the stage select's first page.
nav.STAGES.setdefault("bowsers_castle", (0x1C, 8))
STAGE = ("tech-stage", o_tech_stage)
# Run in a match on Battlefield (three pass-through platforms).
BATTLEFIELD = "battlefield"
PLACE = ("place-platforms", o_place_platforms)
TECH_RAY_EDGE = ("tech-ray-edge", o_tech_ray_edge)
AIR_JUMP_FLY = ("air-jump-fly", o_air_jump_fly)
THROW_LAYDOWN = ("throw-laydown", o_throw_laydown)
TECH_SLIP = ("tech-slip", o_tech_slip)
TECH_INVIS_MARIO = ("tech-invis-mario", o_tech_invis)
TECH_INVIS_SHEIK = ("tech-invis-sheik", o_tech_invis_sheik)
AUTO_LCANCEL_TETHER = ("auto-lcancel-tether", o_auto_lcancel_tether)
# The P+ character techniques against characters that have them, and Wolf.
OOS_OTHERS = [(f"oos-techniques-{c}", o_oos_techniques) for c in ("wolf", "ness", "lucas", "peach", "mewtwo", "ganondorf", "yoshi")]
# Projectile / thrown-item powershield scenarios in their own matches.
PS_FOX = ("powershield-fox", o_powershield_fox)
PS_ITEM = ("powershield-item", o_powershield_item)
PS_CHARS = [
    (("powershield-yoshi-fox", make_ps_char("Fox laser", (-30.0, 30.0), 80)), "fox", "yoshi"),
    # Placed so the bouncing fireball meets Yoshi's curled r=6 egg rather than
    # peaking at its standing head 3-4 frames ahead of the egg.
    (("powershield-yoshi-mario", make_ps_char("Mario fireball", (-15.0, 20.0), 120)), "mario", "yoshi"),
    (("powershield-bowser-fox", make_ps_char("Fox laser", (-30.0, 30.0), 80)), "fox", "bowser"),
    (("powershield-bowser-mario", make_ps_char("Mario fireball", (-25.0, 25.0), 120)), "mario", "bowser"),
    (("powershield-jigglypuff-fox", make_ps_char("Fox laser", (-30.0, 30.0), 80)), "fox", "jigglypuff"),
    (("powershield-jigglypuff-mario", make_ps_char("Mario fireball", (-25.0, 25.0), 120)), "mario", "jigglypuff"),
]
# The scenarios of the other matches that start from the hit states.
FROM_HIT_STATES = [THROW_LAYDOWN[0], TECH_SLIP[0], TECH_INVIS_MARIO[0], AIR_JUMP_FLY[0]] + [name for name, _ in OOS_OTHERS]

# Shard units in the order cmd_test_cpu plays them.
# MEASURE scenarios run only with --only and are left out.
SHARD_UNITS = (
    [name for name, _ in SCENARIOS]
    + [NANA[0], STAGE[0], PLACE[0], TECH_RAY_EDGE[0], AIR_JUMP_FLY[0]]
    + [THROW_LAYDOWN[0], TECH_SLIP[0]]
    + [name for name, _ in OOS_OTHERS]
    + [PS_FOX[0], PS_ITEM[0]]
    + [spec[0] for spec, _, _ in PS_CHARS]
    + [TECH_INVIS_SHEIK[0]]
    + [AUTO_LCANCEL_TETHER[0]]
    + [TECH_INVIS_MARIO[0]]
)
# Seconds per scenario (for --shard balancing; unlisted scenarios take the mean).
SECONDS = {
    "c1": 4, "c2": 5, "c3": 2, "c4": 3, "c5": 45, "e": 2, "di": 15, "tech": 18, "shield": 10,
    "counter-ground": 18, "counter-delay-land": 15, "counter-delay-tumble": 20, "counter-shield": 6,
    "shield-slideoff": 12, "shield-oos-retry": 12,
    "counter-air": 13, "tdi": 5, "sdi": 5, "dual-stick-sdi": 8, "own-hitlag": 10, "techopts": 39, "techrandom": 74,
    "silence": 14, "intangible": 2, "menu": 3, "training": 6,
    "multi": 3, "actions-ground": 120, "actions-air": 113, "actions-shield": 86, "shield-angle": 10,
    "powershield": 6, "powershield-cancel": 8, "asdi": 5, "asdi-probe": 7, "tdi-measured": 10,
    "tdi-angles": 43, "cue": 65, "landing": 12, "shield-end": 45, "wiggle": 19, "air-jump": 17, "air-jump-empty": 20,
    "move-freeze": 20, "grab": 38, "grab-airborne": 16, "advanced": 22,
    "tech-wait": 10, "tech-trap": 114, "tech-lockout": 116, "tech-damage-air": 20, "tech-edge": 18, "tech-drift": 35, "wall-tech": 25,
    "percent-lock": 4,
    "throw-behavior": 20, "counter-tumble": 25, "after-tech-counter": 12,
    "auto-lcancel": 18, "shffl-timing": 20, "ffl-air": 16, "auto-lcancel-tether": 20,
    "oos-techniques": 25, "finish": 3,
    "nana": 8, "tech-stage": 47, "place-platforms": 17, "tech-ray-edge": 20, "air-jump-fly": 20,
    "throw-laydown": 20, "tech-slip": 35,
    "oos-techniques-wolf": 25, "oos-techniques-ness": 25, "oos-techniques-lucas": 25,
    "oos-techniques-peach": 25, "oos-techniques-mewtwo": 25, "oos-techniques-ganondorf": 25, "oos-techniques-yoshi": 25,
    "powershield-fox": 20, "powershield-item": 20,
    "powershield-yoshi-fox": 15, "powershield-yoshi-mario": 15,
    "powershield-bowser-fox": 15, "powershield-bowser-mario": 15,
    "powershield-jigglypuff-fox": 15, "powershield-jigglypuff-mario": 15,
}


def cpu_report(log):
    """For test-stress: the fighter-input hook went in once per boot and
    every match released what it took."""
    installs = re.findall(L.rx(r"\[{name}\] hook fighter input: installed at ([0-9a-f]{{8}})").pattern, log)
    if len(installs) != 1:
        return f"the fighter-input hook was installed {len(installs)} times"
    for takes, releases, back in re.findall(r"cpu: (\d+) takes, (\d+) releases, (\d+) AI flags put back", log):
        if takes != releases:
            return f"a match ended with {takes} takes and {releases} releases"
    return ""


def run_scenario(name, fn, r):
    """Runs one scenario; a Python error in its checks fails that scenario
    only (with the traceback), so the others still run."""
    try:
        res = fn(r)
    except (KeyError, IndexError, TypeError, ValueError, AttributeError, ZeroDivisionError, NameError, CpuError) as e:
        import traceback
        res = Result(name)
        res.check(f"{name} stopped: {e!r}", False)
        for line in traceback.format_exc().splitlines()[-6:]:
            res.note(line)
    res.key = name
    print(f"{res.name}: {'PASS' if res.ok else 'FAIL'}", flush=True)
    for line in res.lines:
        print("  " + line, flush=True)
    return res


def cmd_test_cpu(rest):
    ap = argparse.ArgumentParser(prog="emu.py test-cpu")
    ap.add_argument("rel")
    ap.add_argument("--only", nargs="*", default=None)
    ap.add_argument("--cpu", default="fox", help="the CPU character of the main match (nav.CSS_GRID name)")
    ap.add_argument("--aspect", choices=("16:9", "4:3"), help="the game's screen setting (default: as the save has it)")
    ap.add_argument("--shard", default=None, metavar="I/N",
                    help="play group I of the N groups the scenarios are divided into (SECONDS)")
    ap.add_argument("--list", action="store_true", help="print the scenarios this run would play, and stop")
    opts = ap.parse_args(rest)
    chosen = E.shard(SHARD_UNITS, SECONDS, opts.shard)
    chosen_set = set(chosen)
    wanted = lambda name: name in chosen_set and (not opts.only or name in opts.only)  # noqa: E731
    wanted_measure = lambda name: bool(opts.only) and not opts.shard and name in opts.only  # noqa: E731
    if opts.list:
        for name in chosen:
            if not opts.only or name in opts.only:
                print(name)
        for name, _ in MEASURE:
            if wanted_measure(name):
                print(name)
        return 0
    label = "test-cpu"
    backend = E.headless_backend()
    t0 = time.monotonic()
    why, step, log_path, results, times = "", "boot", "", [], []
    try:
        with E.harness_run(backend, E.DETERMINISTIC_ARGS, allow_null=True) as (proc, emu):
            log_path = emu.log_path
            r = Runner(emu)
            r.cpu_name = opts.cpu
            try:
                step = "boot"
                nav.boot_to_training_css(emu)
                if opts.aspect:
                    LT.set_aspect(emu, opts.aspect, print)
                need_main = (any(wanted(name) for name, _ in SCENARIOS)
                             or any(wanted_measure(name) for name, _ in MEASURE))
                match = None
                if need_main:
                    step = "into the match"
                    nav.start_training_match(emu, "mario", opts.cpu)
                    emu.advance(60)
                    match = ("mario", opts.cpu, "final_destination")
                    step = "savestates"
                    prepare(r)
                for name, fn in SCENARIOS + MEASURE:
                    # The measurements run only when asked for by name (never in a shard).
                    is_measure = (name, fn) in MEASURE
                    if is_measure and not wanted_measure(name):
                        continue
                    if not is_measure and not wanted(name):
                        continue
                    step = name
                    # Every scenario starts with the game running, whatever
                    # the one before it (or prepare) left, so each also runs
                    # alone with --only.
                    emu.h.call("resume")
                    t1 = time.monotonic()
                    res = run_scenario(name, fn, r)
                    results.append(res)
                    times.append((name, time.monotonic() - t1))
                # (The match against a Mario CPU comes last: with both tokens
                # on one icon the character select is not left again.)
                others = [(NANA, "mario", "ice_climbers", "final_destination"),
                          (STAGE, "mario", opts.cpu, "bowsers_castle"),
                          (PLACE, "mario", opts.cpu, BATTLEFIELD),
                          (TECH_RAY_EDGE, "mario", opts.cpu, BATTLEFIELD),
                          (AIR_JUMP_FLY, "mario", "jigglypuff", "final_destination")]
                others += [(THROW_LAYDOWN, "snake", "fox", "final_destination")]
                others += [(TECH_SLIP, "diddy_kong", "fox", "final_destination")]
                others += [(one, "mario", one[0].rsplit("-", 1)[1], "final_destination") for one in OOS_OTHERS]
                others += [(PS_FOX, "fox", "mario", "final_destination")]
                others += [(PS_ITEM, "peach", "fox", "final_destination")]
                others += [(spec, p1, cpu, "final_destination") for spec, p1, cpu in PS_CHARS]
                others += [(TECH_INVIS_SHEIK, "mario", "zelda", "final_destination")]
                others += [(AUTO_LCANCEL_TETHER, "mario", "link", "final_destination")]
                others += [(TECH_INVIS_MARIO, "mario", "mario", "final_destination")]
                for (name, fn), p1, cpu, stage in others:
                    if not wanted(name):
                        continue
                    step = f"{name} match"
                    t1 = time.monotonic()
                    emu.h.call("resume")
                    if match != (p1, cpu, stage):
                        if nav.scene(emu)[0] == "scMelee":
                            nav.finish_training(emu)
                        nav.start_training_match(emu, p1, cpu, stage)
                        emu.advance(60)
                        match = (p1, cpu, stage)
                        r.cpu_name = cpu
                        if name in FROM_HIT_STATES:
                            # The scenario starts from the hit states.
                            step = f"{name} savestates"
                            prepare(r)
                    r.refresh()
                    step = name
                    res = run_scenario(name, fn, r)
                    results.append(res)
                    times.append((name, time.monotonic() - t1))
                step = "finish"
                emu.h.call("resume")
                if nav.scene(emu)[0] == "scMelee":
                    nav.finish_training(emu)
                emu.advance(60)
            except (Timeout, nav.NavError, HarnessError, CpuError, LT.LabError, PI.InputError) as e:
                why = f"{step}: {e}"
                try:
                    emu.h.call("resume")
                    emu.screenshot("fail-cpu")
                except (OSError, HarnessError, ValueError):
                    pass
    except (OSError, HarnessError, Timeout, ValueError) as e:
        why = why or f"{step}: {e}"
    log = E.read_log(log_path) if log_path else ""
    print()
    for line in log.splitlines():
        if re.search(rf"\[{L.tag()}\] (hook fighter|module MTCH|cpu: (take|release|slot|\d+ takes))", line):
            print("  " + line)
    bad = L.problems(log)
    for line in bad:
        print(f"problem: {line}")
    if not why and bad:
        why = "problem lines in the log"
    if not why:
        why = cpu_report(log)
    print()
    print(f"{'scenario':<26} {'check':<58} {'expected':<28} observed")
    for res in results:
        for name, what, exp, obs, ok in res.table:
            print(f"{'' if ok else '!'}{name:<25} {what[:58]:<58} {exp[:28]:<28} {obs}")
    if times:
        print("seconds per scenario: " + ", ".join(f"{name} {t:.0f}" for name, t in times))
    print(f"time: {time.monotonic() - t0:.1f}s")
    if not why and not all(res.ok for res in results):
        print("failed --only: " + " ".join(res.key for res in results if not res.ok))
        why = "a scenario failed: " + ", ".join(res.name for res in results if not res.ok)
    if not why and not results:
        why = "no scenario ran"
    if why:
        return L.report(log_path, why, label)
    print(f"{label}: PASS")
    return 0
