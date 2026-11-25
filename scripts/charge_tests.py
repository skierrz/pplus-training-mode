"""Emulator tests of character charges across savestates and recording positions
(run through emu.py; settings come from the environment the same way).

  test-charges REL [--only NAME ...] [--keep-going]

One boot on Final Destination, switching matches via nav.start_quick_match.
Every charge, save (hold D-pad right), spend, load (press D-pad left), and
Recording page Save/Restore Positions goes through the real controller path
on port 1 while fighter work variables, damage, status sound handles and
active articles are read directly from game memory.

Scenarios:
  lucas         Offense Up (RA-Bit[107], LA-Float[61]): no-charge load, full
                charge + smash damage (and single-smash consumption), mid-charge
                save while B is held, Recording page Save/Restore Positions
  samus         Charge Shot (LA-Basic[65], wnSamusCShot): no-charge, partial,
                full, mid-charge while B is held, Recording Save/Restore
  mewtwo        Shadow Ball (LA-Basic[65], wnLucarioAuraBall): no-charge,
                partial, full, mid-charge while B is held, Recording Save/Restore
  lucario       Aura Sphere (LA-Basic[65], wnLucarioAuraBall) + P+ Aura Charge
                (LA-Float[27]) + damage-based aura check (LA-Float[19]):
                no-charge, partial, full, mid-charge, Recording Save/Restore
  donkey_kong   Giant Punch (LA-Basic[64]): no-charge, partial, full, mid-charge
  ivysaur       Solarbeam (LA-Float[27]): no-charge, full, mid-charge
  wario         Waft (LA-Basic[65] timer, LA-Basic[66] stage): early-state load,
                charged timer spend and restore
  squirtle      Water Gun (LA-Basic[64]): no-charge, partial, full, mid-charge
  rob           Gyro (LA-Float[20]) and Robo Beam (LA-Float[19]): no-charge,
                partial/full Gyro, mid-charge Gyro, Robo Beam timer restore
  lite          Three CPUs (4 fighters): Recording Save/Restore Positions falls
                back to the lite start state; position, facing, damage and
                shield restore while charge state does not
"""
import argparse
import re
import struct
import sys
import time

sys.dont_write_bytecode = True

import emu as E  # noqa: E402
import lab_tests as LT  # noqa: E402
import loader_tests as L  # noqa: E402
import nav  # noqa: E402
from harness import HarnessError, Timeout  # noqa: E402

# (gmCharacterKind, CSS icon id) from lib/BrawlHeaders gm/gm_lib.h and nav.CSS_ID.
CHARS = {
    "fox": (0x07, 0x07),
    "lucas": (0x1B, 0x19),
    "samus": (0x03, 0x03),
    "mewtwo": (0x33, 0x2E),
    "lucario": (0x24, 0x20),
    "donkey_kong": (0x01, 0x01),
    "squirtle": (0x20, 0x2B),
    "ivysaur": (0x22, 0x2C),
    "rob": (0x26, 0x22),
    "wario": (0x17, 0x15),
    "game_and_watch": (0x14, 0x12),
}

SAVED = re.compile(r"savestate: saved frame (\d+): [^\r\n]+")
LOADED = re.compile(r"savestate: loaded frame (\d+) \(load (\d+)\): [^\r\n]+")
REC_SAVED = re.compile(r"record: positions saved(?: again)? \(([^)]+)\)")
REC_RESTORED = re.compile(r"record: restored \(([^)]+)\)")

RIGHT, LEFT, UP, DOWN = (228, 128), (28, 128), (128, 228), (128, 28)
NEUTRAL = (128, 128)
ST_WAIT = 0x0

# Settings offsets (src/main/settings.h): D-pad bindings, Random Percent.
SET_SS_LEFT = 27
SET_SS_RANDOM = 184
SET_CPU_OPTIONS = LT.SET_CPU_PERCENT + 3


class ChargeError(RuntimeError):
    pass


def u32(b, o):
    return struct.unpack_from(">I", b, o)[0]


def s32(b, o):
    return struct.unpack_from(">i", b, o)[0]


def f32(b, o):
    return struct.unpack_from(">f", b, o)[0]


class FighterView:
    """Reads a fighter's work variables, posture, damage, sound handle and
    active articles directly from memory."""

    def __init__(self, emu, player=0):
        self.emu = emu
        self.f = LT.Fighters(emu)
        self.player = player

    def _entry(self):
        en = self.f.by_player(self.player)
        if en is None:
            raise ChargeError(f"player {self.player} fighter not found")
        return en

    def _mods(self):
        return self.f.modules(self._entry())

    def _work(self, which):
        # soWorkManageModuleImpl: +0x20 LA (instance), +0x24 RA (status).
        # soGeneralWorkSimple: +0x0C ints, +0x14 floats, +0x1C flags.
        wm = u32(self._mods(), 0x64)
        return self.emu.ptr(wm + which)

    def la_int(self, idx):
        g = self._work(0x20)
        return s32(self.emu.read(self.emu.ptr(g + 0x0C) + 4 * idx, 4), 0)

    def la_float(self, idx):
        g = self._work(0x20)
        return f32(self.emu.read(self.emu.ptr(g + 0x14) + 4 * idx, 4), 0)

    def la_bit(self, idx):
        g = self._work(0x20)
        word = self.emu.u32(self.emu.ptr(g + 0x1C) + 4 * (idx >> 5))
        return 1 if (word & (1 << (idx & 31))) else 0

    def ra_int(self, idx):
        g = self._work(0x24)
        return s32(self.emu.read(self.emu.ptr(g + 0x0C) + 4 * idx, 4), 0)

    def ra_float(self, idx):
        g = self._work(0x24)
        return f32(self.emu.read(self.emu.ptr(g + 0x14) + 4 * idx, 4), 0)

    def ra_bit(self, idx):
        # Project+ accesses RA-Bit[107] (0x2200006B) via soGeneralWorkSimple::isFlag
        # without a bounds check, indexing m_flagWork[idx >> 5].
        g = self._work(0x24)
        word = self.emu.u32(self.emu.ptr(g + 0x1C) + 4 * (idx >> 5))
        return 1 if (word & (1 << (idx & 31))) else 0

    def damage(self):
        return self.f.damage(self._entry())

    def shield(self):
        return self.f.shield(self._entry())

    def status(self):
        return self.emu.u32(u32(self._mods(), 0x70) + 0x34)

    def posture(self):
        p = self.emu.read(u32(self._mods(), 0x0C) + 0x0C, 0x38)
        return {"x": f32(p, 0), "y": f32(p, 4), "lr": f32(p, 0x34)}

    def status_sound_handle(self):
        # soSoundModuleImpl +0x38: active status looping sound handle (savestate.cpp).
        snd = u32(self._mods(), 0x50)
        return self.emu.u32(snd + 0x38) if self.emu.is_ram(snd) else 0

    def active_articles(self):
        """Read the live count of active articles from
        soGenerateArticleManageModuleImpl (+0x84 in soModuleEnumeration,
        soArrayVector<soArticle*, C> at +0x2C) by decoding its size() bitfield
        getter (vtable +0x14: lwz r0,8(r3); rlwinm r0,r0,SH,0,ME; srawi r3,r0,SR)."""
        mgr = u32(self._mods(), 0x84)
        if not self.emu.is_ram(mgr):
            return 0
        arr = self.emu.ptr(mgr + 0x2C)
        if not arr:
            return 0
        vt = self.emu.ptr(arr)
        if not vt:
            return 0
        sz_fn = self.emu.ptr(vt + 0x14)
        if not sz_fn:
            return 0
        w0, w1, w2 = struct.unpack(">3I", self.emu.read(sz_fn, 12))
        word = self.emu.u32(arr + 0x08)
        # soArrayVector::size(): lwz r0,8(r3); rlwinm r0,r0,sh,mb,me; srawi r3,r0,sr
        if (w1 >> 26) == 21 and (w2 >> 26) == 31:
            sh = (w1 >> 11) & 31
            mb = (w1 >> 6) & 31
            me = (w1 >> 1) & 31
            sr = (w2 >> 11) & 31
            rot = ((word << sh) | (word >> ((32 - sh) & 31))) & 0xFFFFFFFF
            mask = ((1 << (32 - mb)) - 1) ^ ((1 << (31 - me)) - 1)
            return (rot & mask) >> sr
        return 0


# ---- real-controller helpers ---------------------------------------------------

def _pad_step(buttons=(), stick=NEUTRAL, cstick=NEUTRAL, hold=1):
    return {
        "buttons": list(buttons),
        "main": list(stick),
        "c": list(cstick),
        "l": 255 if "L" in buttons else 0,
        "r": 255 if "R" in buttons else 0,
        "hold": hold,
    }


def run_pad_script(emu, steps, port=0):
    """Play frame-exact pad steps via Dolphin's pad_script so short button
    presses and multi-step inputs cannot stretch or race on unpaused headless
    runs. Always restores neutral on the following poll."""
    emu.neutral(port)
    seq = list(steps) + [_pad_step(hold=1)]
    result = emu.h.call("pad_script", port=port, frames=seq, start="next")
    emu.wait_polls(int(result["ends_at"]))


def wait_until_frame(emu, target, timeout_s=30.0):
    """Wait until nav.game_frame(emu) reaches `target`."""
    t0 = time.monotonic()
    while True:
        cur = nav.game_frame(emu)
        if cur >= target:
            return cur
        if time.monotonic() - t0 > timeout_s:
            raise Timeout(f"game frame did not reach {target} (at {cur})")
        gap = target - cur
        emu.advance(min(gap - 1, 4) if gap > 2 else 1)


def wait_game_frames(emu, n):
    wait_until_frame(emu, nav.game_frame(emu) + n)


def wait_idle(emu, p1, timeout=180):
    t0 = nav.game_frame(emu)
    while nav.game_frame(emu) - t0 < timeout:
        if p1.status() == ST_WAIT:
            wait_game_frames(emu, 4)
            if p1.status() == ST_WAIT:
                return True
        emu.advance(2)
    return p1.status() == ST_WAIT


def wait_cond(emu, what, fn, timeout=240, step=2):
    t0 = nav.game_frame(emu)
    p0 = emu.polls()
    poll_limit = max(timeout * 2, timeout + 120)
    while True:
        v = fn()
        if v:
            return v
        cur = nav.game_frame(emu)
        # Bound by both game frames and input polls: savestate loads move
        # nav.game_frame backwards and paused menus freeze it.
        if (cur >= t0 and cur - t0 >= timeout) or (emu.polls() - p0 >= poll_limit):
            raise Timeout(f"timed out waiting for {what} after {timeout} frames")
        emu.advance(step)


def dpad_save(emu, extra=(), stick=NEUTRAL, release_all=True):
    """Hold D-pad right (plus any `extra` buttons/stick) until the plugin logs
    the save. With release_all=True, returns to neutral and waits past
    savestate.cpp's 24-frame D-pad lockout anchored to the saved frame."""
    pos = len(emu.synced_log_text() if release_all else emu.log_text())
    btns = list(extra) + ["DRIGHT"]
    emu.hold(0, buttons=btns, stick=stick)
    m = wait_cond(emu, "D-pad right save", lambda: SAVED.search(emu.log_text(), pos), timeout=90, step=1)
    saved_frame = int(m.group(1))
    if release_all:
        emu.neutral(0)
        wait_game_frames(emu, 4)
        wait_until_frame(emu, saved_frame + 30)
    else:
        if extra or stick != NEUTRAL:
            emu.hold(0, buttons=list(extra), stick=stick)
        else:
            emu.neutral(0)
    return saved_frame, m.group(0)


def dpad_load(emu, extra=(), stick=NEUTRAL, release_all=True):
    """Press D-pad left (plus any `extra` buttons/stick) until the plugin logs
    the load."""
    pos = len(emu.synced_log_text() if release_all else emu.log_text())
    btns = list(extra) + ["DLEFT"]
    emu.hold(0, buttons=btns, stick=stick)
    m = wait_cond(emu, "D-pad left load", lambda: LOADED.search(emu.log_text(), pos), timeout=90, step=1)
    if release_all:
        emu.neutral(0)
        wait_game_frames(emu, 4)
    else:
        if extra or stick != NEUTRAL:
            emu.hold(0, buttons=list(extra), stick=stick)
        else:
            emu.neutral(0)
    return int(m.group(1)), m.group(0)


def wait_cpu_standing(emu, cpu, timeout=420):
    """Wait until CPU is standing idle onstage (not mid-recovery, on ledge, or
    sitting on Brawl's 300-frame revival platform; FD floor is x = -85.5..+85.5)."""
    wait_cond(
        emu,
        "CPU standing on stage",
        lambda: cpu.status() == ST_WAIT and abs(cpu.posture()["y"]) < 3.0 and abs(cpu.posture()["x"]) < 84.0,
        timeout=timeout,
        step=2,
    )


def wait_x_settled(emu, fv, timeout=45):
    """Wait until `fv`'s horizontal position stops sliding across distinct game
    frames (important for low-traction characters like Squirtle and post-push
    CPU slides)."""
    last_f = [nav.game_frame(emu)]
    last_x = [fv.posture()["x"]]
    stable = [0]

    def settled():
        f = nav.game_frame(emu)
        if f <= last_f[0]:
            return False
        x = fv.posture()["x"]
        if abs(x - last_x[0]) < 0.05:
            stable[0] += 1
        else:
            stable[0] = 0
        last_f[0] = f
        last_x[0] = x
        return stable[0] >= 2

    wait_cond(emu, f"player {fv.player} horizontal slide settle", settled, timeout=timeout, step=1)


def place_cpu(emu, p1, cpu, close_dist=None):
    """Wait until CPU is standing on stage, then walk P1 to `close_dist`
    (default 16 units) to the left of CPU (or right if near the left ledge)
    and face CPU."""
    target = close_dist if close_dist is not None else 16.0
    wait_cpu_standing(emu, cpu, timeout=420)
    want_left = cpu.posture()["x"] - target > -35.0
    t0 = nav.game_frame(emu)
    while nav.game_frame(emu) - t0 < 240:
        cx = cpu.posture()["x"]
        px = p1.posture()["x"]
        goal = (cx - target) if want_left else (cx + target)
        err = goal - px
        if abs(err) <= 2.0 and abs(cx - px) <= target + 1.0:
            break
        if abs(err) < 6.0:
            emu.hold(0, stick=(168, 128) if err > 0 else (88, 128))
            emu.advance(1)
        else:
            emu.hold(0, stick=(196, 128) if err > 0 else (60, 128))
            emu.advance(2)
    emu.neutral(0)
    wait_idle(emu, p1, 60)
    wait_x_settled(emu, p1, 30)
    # Ensure P1 is facing the CPU before attacking.
    p1p, cpup = p1.posture(), cpu.posture()
    dx = cpup["x"] - p1p["x"]
    if dx != 0.0 and (dx > 0) != (p1p["lr"] > 0):
        want_right = dx > 0
        emu.hold(0, stick=(164, 128) if want_right else (92, 128))
        wait_cond(emu, "P1 face CPU", lambda: (p1.posture()["lr"] > 0) == want_right, timeout=40, step=1)
        emu.neutral(0)
        wait_idle(emu, p1, 60)
        wait_x_settled(emu, p1, 30)


def rec_save_positions(emu, lab):
    """Drive Start -> Recording -> Save Positions -> Start through the lab menu."""
    pos = len(emu.synced_log_text())
    lab.open_menu()
    lab.main_to(LT.MAIN_RECORDING)
    lab.tap(["A"])
    lab.tap(["A"])  # Save Positions
    lab.close_menu()
    m = wait_cond(emu, "Recording Save Positions", lambda: REC_SAVED.search(emu.log_text(), pos), timeout=120, step=2)
    wait_game_frames(emu, 6)
    return m.group(1), m.group(0)


def rec_resave_positions(emu, lab):
    """Drive Start -> Recording -> Re-Save Positions -> Start through the lab menu."""
    pos = len(emu.synced_log_text())
    lab.open_menu()
    lab.main_to(LT.MAIN_RECORDING)
    lab.tap(["A"])
    # Rows 3-6 (CPU Mode/Slot, Mirrored Playback, CPU Counter) are disabled
    # when CPU Control is off and skipped by navigation, so row 11 is 7 downs.
    lab.down(7)
    lab.tap(["A"])
    lab.back_to_main(1)
    lab.close_menu()
    m = wait_cond(emu, "Recording Re-Save Positions", lambda: REC_SAVED.search(emu.log_text(), pos), timeout=120, step=2)
    wait_game_frames(emu, 6)
    return m.group(1), m.group(0)


def rec_restore_positions(emu, lab):
    """Drive Start -> Recording -> Restore Positions -> Start through the lab menu."""
    pos = len(emu.synced_log_text())
    lab.open_menu()
    lab.main_to(LT.MAIN_RECORDING)
    lab.tap(["A"])
    lab.tap(["A"])  # Restore Positions
    lab.close_menu()
    m = wait_cond(emu, "Recording Restore Positions", lambda: REC_RESTORED.search(emu.log_text(), pos), timeout=120, step=2)
    wait_game_frames(emu, 6)
    return m.group(1), m.group(0)


def fire_and_measure_damage(emu, p1, cpu, buttons=("B",), stick=NEUTRAL, c=NEUTRAL, hold_f=3, wait_f=70):
    """Perform an attack/shot and return the damage dealt to `cpu`."""
    before = cpu.damage()
    run_pad_script(emu, [_pad_step(buttons=buttons, stick=stick, cstick=c, hold=hold_f)])
    t0 = nav.game_frame(emu)
    while nav.game_frame(emu) - t0 < wait_f:
        if cpu.damage() > before + 0.01:
            wait_game_frames(emu, 16)
            break
        emu.advance(2)
    wait_idle(emu, p1, 120)
    after = cpu.damage()
    return round(after - before, 2)


def set_p1_damage(emu, p1, value):
    en = p1._entry()
    emu.h.call("write_mem", addr=emu.ptr(en["owner"]) + 0x24, hex=struct.pack(">f", float(value)).hex())


def prepare_match(emu, char_name, cpu_name="fox"):
    """From Training's character select: quick-start `char_name` vs `cpu_name` on
    Final Destination, reset lab settings this test relies on, wait for idle."""
    if nav.scene(emu)[0] == "scMelee":
        nav.finish_training(emu)
    if nav.scene(emu)[0] != "scSelctCharacter":
        nav.boot_to_training_css(emu)
    emu.mark(f"charge test: {char_name} vs {cpu_name}")
    nav.start_quick_match(emu, stage="final_destination", p1=CHARS[char_name], cpu=CHARS[cpu_name], seed_rng=True)
    emu.neutral(0)
    lab = LT.Lab(emu, [])
    settings = lab.find_settings()
    emu.h.call("write_mem", addr=settings + LT.SET_CPU_PERCENT, hex="000000")
    emu.h.call("write_mem", addr=settings + SET_CPU_OPTIONS, hex="00")
    emu.h.call("write_mem", addr=settings + LT.SET_FRAME_ADVANCE, hex="00")
    emu.h.call("write_mem", addr=settings + SET_SS_LEFT, hex="0000")
    emu.h.call("write_mem", addr=settings + SET_SS_RANDOM, hex="0000")
    wait_game_frames(emu, 40)
    p1 = FighterView(emu, 0)
    cpu = FighterView(emu, 1)
    wait_idle(emu, p1, 120)
    # Flush the match-start auto-save log line before any scenario starts.
    emu.synced_log_text()
    return lab, p1, cpu


def check_clean_idle(p1, idle_snd, log_slice, fails, label):
    arts = p1.active_articles()
    snd = p1.status_sound_handle()
    probs = L.problems(log_slice)
    if arts != 0:
        fails.append(f"{label}: {arts} active article(s) left hanging")
    if snd != idle_snd:
        fails.append(f"{label}: status sound handle {snd:#x} != idle {idle_snd:#x}")
    if probs:
        fails.append(f"{label}: problem line(s) in log: {probs[0]}")
    return arts, snd


# ---- character scenarios -------------------------------------------------------

def sc_lucas(emu, notes):
    lab, p1, cpu = prepare_match(emu, "lucas")
    fails = []
    idle_snd = p1.status_sound_handle()
    place_cpu(emu, p1, cpu, close_dist=10.0)

    def whiff_fsmash_away():
        """Step away from CPU so F-Smash's backswing cannot clip and refund
        RA-Bit[107] (Subr@0x28544), then hold C-stick away until AttackS4 starts."""
        away = LEFT if cpu.posture()["x"] >= p1.posture()["x"] else RIGHT
        if abs(cpu.posture()["x"] - p1.posture()["x"]) < 22.0:
            emu.hold(0, stick=away)
            wait_cond(
                emu,
                "Lucas spacing >= 22 before whiff",
                lambda: abs(cpu.posture()["x"] - p1.posture()["x"]) >= 22.0,
                timeout=60,
                step=1,
            )
            emu.neutral(0)
            wait_idle(emu, p1, 60)
        away = LEFT if cpu.posture()["x"] >= p1.posture()["x"] else RIGHT
        emu.hold(0, cstick=away)
        wait_cond(emu, "Lucas AttackS4 start", lambda: p1.status() in (0x2A, 0x2B, 0x2C), timeout=30, step=1)
        emu.neutral(0)
        wait_idle(emu, p1, 120)

    # (c) No-charge save, charge fully, load no-charge -> must be 0 & unpowered damage.
    _, s_no = dpad_save(emu)
    emu.hold(0, buttons=["B"])
    wait_cond(emu, "Lucas Offense Up", lambda: p1.ra_bit(107) == 1, timeout=220)
    emu.neutral(0)
    wait_idle(emu, p1, 120)
    charged_pre = p1.ra_bit(107)
    _, l_no = dpad_load(emu)
    after_no = (p1.ra_bit(107), round(p1.la_float(61), 2))
    base_dmg = fire_and_measure_damage(emu, p1, cpu, buttons=(), c=RIGHT, hold_f=3, wait_f=60)
    if charged_pre != 1 or after_no != (0, 0.0):
        fails.append(f"no-charge load: pre={charged_pre}, after={after_no}, wanted (0, 0.0)")

    # Full charge: save, spend by whiffing F-Smash away from CPU (Offense Up is consumed
    # on whiff, while Subr@0x28544 refunds RA-Bit[107] when a smash connects on an opponent),
    # load, verify RA-Bit[107]==1, then hit CPU to confirm powered damage and on-hit refund.
    emu.hold(0, buttons=["B"])
    wait_cond(emu, "Lucas Offense Up full", lambda: p1.ra_bit(107) == 1, timeout=220)
    emu.neutral(0)
    wait_idle(emu, p1, 120)
    place_cpu(emu, p1, cpu, close_dist=10.0)
    saved_val = p1.ra_bit(107)
    _, s_full = dpad_save(emu)
    whiff_fsmash_away()
    after_spend = p1.ra_bit(107)
    _, l_full = dpad_load(emu)
    after_load = p1.ra_bit(107)
    # Hit CPU -> powered F-Smash deals boosted damage and Subr@0x28544 refunds RA-Bit[107].
    toward = RIGHT if cpu.posture()["x"] >= p1.posture()["x"] else LEFT
    load_dmg = fire_and_measure_damage(emu, p1, cpu, buttons=(), c=toward, hold_f=3, wait_f=60)
    after_hit = p1.ra_bit(107)
    place_cpu(emu, p1, cpu, close_dist=10.0)
    toward = RIGHT if cpu.posture()["x"] >= p1.posture()["x"] else LEFT
    second_dmg = fire_and_measure_damage(emu, p1, cpu, buttons=(), c=toward, hold_f=3, wait_f=60)
    if saved_val != 1 or after_spend != 0 or after_load != 1 or after_hit != 1:
        fails.append(f"full: saved={saved_val} whiff_spend={after_spend} load={after_load} hit_refund={after_hit}")
    if not (load_dmg > base_dmg + 1.0 and second_dmg > base_dmg):
        fails.append(f"full dmg: base={base_dmg} powered={load_dmg} 2nd_hit={second_dmg}")

    # Mid-charge save while holding B (LA-Float[61] > 0). Whiff F-Smash twice so both
    # RA-Bit[107] and RA-Bit[108] are cleared before starting a fresh Offense Up charge.
    for _ in range(2):
        whiff_fsmash_away()
    pos_mid = len(emu.synced_log_text())
    wait_game_frames(emu, 6)
    seen_st = set()
    max_la61 = [0.0]

    def lucas_mid_ready():
        seen_st.add(p1.status())
        v = p1.la_float(61)
        if v > max_la61[0]:
            max_la61[0] = v
        return v >= 30.0

    emu.hold(0, buttons=["B"])
    try:
        wait_cond(emu, "Lucas mid-charge LA-Float[61] >= 30", lucas_mid_ready, timeout=200)
    except Timeout:
        raise ChargeError(
            f"Lucas mid-charge timed out: status={p1.status():#x} la61={p1.la_float(61):.2f} "
            f"max_la61={max_la61[0]:.2f} ra107={p1.ra_bit(107)} ra108={p1.ra_bit(108)} seen={[hex(s) for s in sorted(seen_st)]}"
        )
    _, s_mid = dpad_save(emu, extra=["B"], release_all=False)
    mid_read_frame = nav.game_frame(emu)
    mid_saved = round(p1.la_float(61), 2)
    emu.neutral(0)
    wait_idle(emu, p1, 120)
    wait_game_frames(emu, 36)
    # Load while holding B -> must continue charging to Offense Up.
    _, l_mid_cont = dpad_load(emu, extra=["B"], release_all=False)
    wait_until_frame(emu, mid_read_frame)
    mid_at_load = round(p1.la_float(61), 2)
    wait_cond(emu, "Lucas mid-charge finish", lambda: p1.ra_bit(107) == 1, timeout=220)
    emu.neutral(0)
    wait_idle(emu, p1, 120)
    # Load without holding B -> releases cleanly back to Wait.
    _, l_mid_rel = dpad_load(emu)
    wait_idle(emu, p1, 120)
    arts, snd = check_clean_idle(p1, idle_snd, emu.synced_log_text()[pos_mid:], fails, "lucas mid-charge")
    if mid_saved < 30.0 or mid_at_load < 25.0:
        fails.append(f"mid-charge: saved={mid_saved}, at_load={mid_at_load}")

    # Step 3: Recording page Save/Restore Positions.
    emu.hold(0, buttons=["B"])
    wait_cond(emu, "Lucas Offense Up for recording", lambda: p1.ra_bit(107) == 1, timeout=220)
    emu.neutral(0)
    wait_idle(emu, p1, 120)
    place_cpu(emu, p1, cpu, close_dist=10.0)
    rec_toward = RIGHT if p1.posture()["lr"] > 0 else LEFT
    rec_how_s, rec_s_line = rec_save_positions(emu, lab)
    # Whiff away from CPU to spend Offense Up -> 0, measure unpowered F-Smash at this stale
    # count, then Restore Positions -> 1 and verify powered F-Smash deals 25% more damage.
    whiff_fsmash_away()
    rec_spend_val = p1.ra_bit(107)
    place_cpu(emu, p1, cpu, close_dist=10.0)
    rec_unpow_dir = RIGHT if cpu.posture()["x"] >= p1.posture()["x"] else LEFT
    rec_unpow_dmg = fire_and_measure_damage(emu, p1, cpu, buttons=(), c=rec_unpow_dir, hold_f=3, wait_f=60)
    rec_how_r, rec_r_line = rec_restore_positions(emu, lab)
    rec_load_val = p1.ra_bit(107)
    rec_load_dmg = fire_and_measure_damage(emu, p1, cpu, buttons=(), c=rec_toward, hold_f=3, wait_f=60)
    if rec_how_s != "savestate" or rec_how_r != "savestate" or rec_spend_val != 0 or rec_load_val != 1:
        fails.append(f"recording: save={rec_how_s} restore={rec_how_r} spend={rec_spend_val} load={rec_load_val}")
    if rec_load_dmg <= rec_unpow_dmg + 2.0:
        fails.append(f"recording dmg: powered={rec_load_dmg} unpowered={rec_unpow_dmg}")

    notes.append(
        f"lucas: RA-Bit[107] full saved={saved_val} whiff_spend={after_spend} load={after_load} hit_refund={after_hit}; "
        f"fsmash dmg base={base_dmg} powered={load_dmg} 2nd_hit={second_dmg}; "
        f"no-charge load={after_no}; mid LA-Float[61] saved={mid_saved} loaded={mid_at_load} arts={arts} snd={snd:#x}; "
        f"rec ({rec_how_s}/{rec_how_r}) spend={rec_spend_val} load={rec_load_val} dmg={rec_load_dmg} (vs unpow {rec_unpow_dmg})"
    )
    notes.append(f"  raw: {s_full} | {l_full} | {rec_s_line} | {rec_r_line}")
    return fails


def _charge_shot_character(emu, notes, char_name, partial_target, full_target,
                           uncharged_val=0, cancel_btn="R", full_two_tap=False,
                           hold_statuses=(0x117, 0x118), direct_start_shoot=False):
    """Shared test for Samus, Mewtwo and Lucario neutral-B charge shot
    (LA-Basic[65] charge level, article 0 held while charging)."""
    lab, p1, cpu = prepare_match(emu, char_name)
    fails = []
    idle_snd = p1.status_sound_handle()
    place_cpu(emu, p1, cpu)

    def charge_to(level, cancel=True):
        emu.hold(0, buttons=["B"])
        wait_cond(emu, f"{char_name} LA-Basic[65] >= {level}", lambda: p1.la_int(65) >= level, timeout=240)
        if cancel:
            emu.hold(0, buttons=[cancel_btn])
            wait_game_frames(emu, 6)
            emu.neutral(0)
            wait_idle(emu, p1, 120)
        else:
            emu.neutral(0)

    def fire_two_tap():
        """Tap B to start/resume charge, then tap B again to fire at the stored
        level without charging past it. With `direct_start_shoot=True` (Lucario:
        FitLucario.pac Act 0x112 Subr@0x1a6a0), the second B tap lands inside
        SpecialNStart (0x112) so it transitions directly to SpecialNShoot (0x119)
        without entering SpecialNHold (0x117) and ticking charge up."""
        before = cpu.damage()
        if direct_start_shoot:
            emu.hold(0, buttons=["B"])
            wait_cond(emu, f"{char_name} SpecialNStart (0x112)", lambda: p1.status() == 0x112, timeout=30, step=1)
            run_pad_script(emu, [_pad_step(hold=2), _pad_step(buttons=("B",), hold=3)])
        else:
            run_pad_script(emu, [_pad_step(buttons=("B",), hold=3), _pad_step(hold=2)])
            wait_cond(emu, f"{char_name} charge hold", lambda: p1.status() in hold_statuses, timeout=45, step=1)
            run_pad_script(emu, [_pad_step(buttons=("B",), hold=4)])
        t0 = nav.game_frame(emu)
        while nav.game_frame(emu) - t0 < 70:
            if cpu.damage() > before + 0.01:
                wait_game_frames(emu, 8)
                break
            emu.advance(2)
        wait_idle(emu, p1, 120)
        after = cpu.damage()
        return round(after - before, 2)

    # (c) Save no-charge, charge up, load no-charge -> must be uncharged_val & uncharged shot damage.
    _, s_no = dpad_save(emu)
    charge_to(partial_target, cancel=True)
    pre_no = p1.la_int(65)
    _, l_no = dpad_load(emu)
    after_no = p1.la_int(65)
    base_dmg = fire_two_tap()
    if pre_no < partial_target or after_no != uncharged_val or base_dmg <= 0.0:
        fails.append(f"no-charge load: pre={pre_no} after={after_no} (want {uncharged_val}) base_dmg={base_dmg}")

    # Partial level: save, fire (two-tap), load, fire (two-tap).
    charge_to(partial_target, cancel=True)
    place_cpu(emu, p1, cpu)
    part_saved = p1.la_int(65)
    _, s_part = dpad_save(emu)
    part_fire_frame = nav.game_frame(emu)
    part_dmg = fire_two_tap()
    part_spend = p1.la_int(65)
    _, l_part = dpad_load(emu)
    part_load = p1.la_int(65)
    wait_until_frame(emu, part_fire_frame)
    part_load_dmg = fire_two_tap()
    if not (0 < part_saved < full_target) or part_spend != uncharged_val or part_load != part_saved:
        fails.append(f"partial: saved={part_saved} spend={part_spend} load={part_load}")
    if not (part_dmg > base_dmg + 0.5 and abs(part_load_dmg - part_dmg) < 0.1):
        fails.append(f"partial dmg: base={base_dmg} spend={part_dmg} load={part_load_dmg}")

    def charge_full():
        """Charge all the way to full and return cleanly to Wait. For Mewtwo/Lucario
        (uncharged_val == -1), wait for SpecialNMax (0x118) before shield-cancelling
        so LA-Basic[64] reaches max and the held article is removed."""
        emu.hold(0, buttons=["B"])
        if uncharged_val == -1:
            wait_cond(emu, f"{char_name} SpecialNMax (0x118)", lambda: p1.status() == 0x118, timeout=280)
            emu.hold(0, buttons=[cancel_btn])
            wait_game_frames(emu, 6)
            emu.neutral(0)
        else:
            wait_cond(emu, f"{char_name} full >= {full_target} in Wait",
                      lambda: p1.la_int(65) >= full_target and p1.status() == ST_WAIT, timeout=280)
            emu.neutral(0)
        wait_idle(emu, p1, 140)

    def fire_full():
        """Fire a stored full charge. In P+, Lucario's Act 0x112 (SpecialNStart)
        always transitions to 0x117 -> 0x118 (SpecialNMax) on a single B tap so
        the player can choose B (shoot) or A (Super Aura Sphere), so Lucario
        needs a second B tap to shoot."""
        if full_two_tap:
            return fire_two_tap()
        return fire_and_measure_damage(emu, p1, cpu, buttons=("B",), hold_f=4, wait_f=70)

    # Full charge: save, fire, load, fire.
    charge_full()
    place_cpu(emu, p1, cpu)
    full_saved = p1.la_int(65)
    _, s_full = dpad_save(emu)
    full_fire_frame = nav.game_frame(emu)
    full_dmg = fire_full()
    full_spend = p1.la_int(65)
    _, l_full = dpad_load(emu)
    full_load = p1.la_int(65)
    wait_until_frame(emu, full_fire_frame)
    full_load_dmg = fire_full()
    if full_saved < full_target or full_spend != uncharged_val or full_load != full_saved:
        fails.append(f"full: saved={full_saved} spend={full_spend} load={full_load}")
    if not (full_dmg > part_dmg + 0.5 and abs(full_load_dmg - full_dmg) < 0.1):
        fails.append(f"full dmg: part={part_dmg} spend={full_dmg} load={full_load_dmg}")

    # Mid-charge save while B is held (article is out). Ensure CPU has settled
    # back onstage before charging so Training's recovery cannot clip P1 during
    # the 12-frame D-pad save hold.
    wait_cpu_standing(emu, cpu, timeout=420)
    mid_trigger = 1 if full_target <= 4 else 2
    pos_mid = len(emu.synced_log_text())
    pre_mid = (p1.la_int(65), p1.status(), nav.game_frame(emu))
    emu.hold(0, buttons=["B"])
    wait_cond(emu, f"{char_name} mid-charge >= {mid_trigger}", lambda: p1.la_int(65) >= mid_trigger, timeout=160)
    pre_save = (p1.la_int(65), p1.status(), nav.game_frame(emu))
    mid_arts_at_save = p1.active_articles()
    _, s_mid = dpad_save(emu, extra=["B"], release_all=False)
    mid_read_frame = nav.game_frame(emu)
    mid_saved = p1.la_int(65)
    post_save = (mid_saved, p1.status(), mid_read_frame)
    # Cancel & spend so we leave the charging timeline completely.
    emu.hold(0, buttons=[cancel_btn])
    wait_game_frames(emu, 6)
    emu.neutral(0)
    wait_idle(emu, p1, 120)
    fire_two_tap()
    wait_game_frames(emu, 20)
    # Load while holding B (already held before D-pad left so B has no fresh
    # press edge on the loaded frame) -> continues charging to full, then exit cleanly to Wait.
    emu.hold(0, buttons=["B"])
    wait_game_frames(emu, 2)
    _, l_mid_cont = dpad_load(emu, extra=["B"], release_all=False)
    wait_until_frame(emu, mid_read_frame)
    mid_at_load = p1.la_int(65)
    try:
        if uncharged_val == -1:
            wait_cond(emu, f"{char_name} mid-charge continue to SpecialNMax", lambda: p1.status() == 0x118, timeout=260)
            emu.hold(0, buttons=[cancel_btn])
            wait_game_frames(emu, 6)
            emu.neutral(0)
        else:
            wait_cond(emu, f"{char_name} mid-charge continue to full",
                      lambda: p1.la_int(65) >= full_saved and p1.status() == ST_WAIT, timeout=260)
            emu.neutral(0)
    except Timeout:
        raise ChargeError(
            f"{char_name} mid-charge continue timed out: pre_mid={pre_mid} pre_save={pre_save} "
            f"post_save={post_save} at_load={mid_at_load} cur={p1.la_int(65)} status={p1.status():#x} arts={p1.active_articles()}"
        )
    wait_idle(emu, p1, 140)
    mid_cont_lvl = p1.la_int(65)
    arts1, snd1 = check_clean_idle(p1, idle_snd, emu.synced_log_text()[pos_mid:], fails, f"{char_name} mid-continue (lvl={mid_cont_lvl})")
    # Load and cancel cleanly with shield -> no orphan article or stuck sound.
    _, l_mid_rel = dpad_load(emu)
    emu.hold(0, buttons=[cancel_btn])
    wait_game_frames(emu, 6)
    emu.neutral(0)
    wait_idle(emu, p1, 120)
    arts2, snd2 = check_clean_idle(p1, idle_snd, emu.synced_log_text()[pos_mid:], fails, f"{char_name} mid-cancel")
    if mid_arts_at_save < 1:
        fails.append(f"mid-charge: expected active article at save, got {mid_arts_at_save}")
    if mid_at_load < mid_saved - 1:
        fails.append(f"mid-charge: saved={mid_saved}, at_load={mid_at_load}")

    # Step 3: Recording page Save/Restore Positions.
    charge_full()
    place_cpu(emu, p1, cpu)
    rec_how_s, rec_s_line = rec_save_positions(emu, lab)
    rec_spend_dmg = fire_full()
    rec_spend_val = p1.la_int(65)
    rec_how_r, rec_r_line = rec_restore_positions(emu, lab)
    rec_load_val = p1.la_int(65)
    rec_load_dmg = fire_full()
    if rec_how_s != "savestate" or rec_how_r != "savestate" or rec_spend_val != uncharged_val or rec_load_val != full_saved:
        fails.append(f"recording: save={rec_how_s} restore={rec_how_r} spend={rec_spend_val} load={rec_load_val}")
    if abs(rec_load_dmg - rec_spend_dmg) >= 0.1 or rec_load_dmg <= part_dmg:
        fails.append(f"recording dmg: spend={rec_spend_dmg} load={rec_load_dmg}")

    notes.append(
        f"{char_name}: LA-Basic[65] partial {part_saved}->{part_spend}->{part_load} (dmg {base_dmg}/{part_dmg}/{part_load_dmg}); "
        f"full {full_saved}->{full_spend}->{full_load} (dmg {full_dmg}/{full_load_dmg}); no-charge={after_no}; "
        f"mid saved={mid_saved} (arts={mid_arts_at_save}) loaded={mid_at_load} clean arts={arts1}/{arts2} snd={snd1:#x}/{snd2:#x}; "
        f"rec ({rec_how_s}/{rec_how_r}) load={rec_load_val} dmg={rec_load_dmg}"
    )
    notes.append(f"  raw: {s_full} | {l_full} | {rec_s_line} | {rec_r_line}")
    return lab, p1, cpu, fails


def sc_samus(emu, notes):
    _, _, _, fails = _charge_shot_character(
        emu, notes, "samus", partial_target=3, full_target=7, uncharged_val=0, hold_statuses=(0x118,)
    )
    return fails


def sc_mewtwo(emu, notes):
    _, _, _, fails = _charge_shot_character(emu, notes, "mewtwo", partial_target=3, full_target=6, uncharged_val=-1)
    return fails


def sc_lucario(emu, notes):
    # FitLucario.pac wnLucarioAuraBall Act[2] (@0x11438) maps levels 3 and 4 to the
    # same 21-damage SubAction 5 (@0x17c98); direct_start_shoot fires straight from
    # Act 0x112 (Subr@0x1a6a0) so a stored level-2 sphere cannot tick into level 3.
    lab, p1, cpu, fails = _charge_shot_character(
        emu, notes, "lucario", partial_target=2, full_target=4, uncharged_val=-1,
        full_two_tap=True, direct_start_shoot=True
    )

    # Check Brawl damage-based aura (LA-Float[19]) at 0% vs 100% damage (P+ clamps to 1.0).
    set_p1_damage(emu, p1, 0.0)
    wait_game_frames(emu, 4)
    aura_mul_0 = round(p1.la_float(19), 4)
    set_p1_damage(emu, p1, 100.0)
    wait_game_frames(emu, 4)
    aura_mul_100 = round(p1.la_float(19), 4)
    set_p1_damage(emu, p1, 0.0)
    wait_game_frames(emu, 4)

    # Check P+ Aura Charge (LA-Float[27], 60.0 per charge): no-charge save, build >= 60 via attacks,
    # save, spend with Double Team cancel (Down-B + A -> SpecialLwSplit), load, verify restore & spend.
    _, _ = dpad_save(emu)  # save at current LA-Float[27] (0 or low)
    base_aura = round(p1.la_float(27), 2)
    set_p1_damage(emu, cpu, 0.0)
    place_cpu(emu, p1, cpu, close_dist=10.0)
    for _ in range(12):
        if p1.la_float(27) >= 60.0:
            break
        set_p1_damage(emu, cpu, 0.0)
        toward = RIGHT if cpu.posture()["x"] >= p1.posture()["x"] else LEFT
        fire_and_measure_damage(emu, p1, cpu, buttons=(), c=toward, hold_f=3, wait_f=45)
        place_cpu(emu, p1, cpu, close_dist=10.0)
    built_aura = round(p1.la_float(27), 2)
    if built_aura < 60.0:
        fails.append(f"lucario aura: could not build LA-Float[27] >= 60 (got {built_aura})")
    else:
        # Load earlier low-aura state to verify no-charge/low-charge restore.
        _, _ = dpad_load(emu)
        reverted_aura = round(p1.la_float(27), 2)
        if abs(reverted_aura - base_aura) > 0.1:
            fails.append(f"lucario aura no-charge load: got {reverted_aura}, wanted {base_aura}")
        # Rebuild >= 60.0, save, spend via Down-B + A (SpecialLwSplit: -60.0), load, spend again.
        set_p1_damage(emu, cpu, 0.0)
        place_cpu(emu, p1, cpu, close_dist=10.0)
        for _ in range(12):
            if p1.la_float(27) >= 60.0:
                break
            set_p1_damage(emu, cpu, 0.0)
            toward = RIGHT if cpu.posture()["x"] >= p1.posture()["x"] else LEFT
            fire_and_measure_damage(emu, p1, cpu, buttons=(), c=toward, hold_f=3, wait_f=45)
            place_cpu(emu, p1, cpu, close_dist=10.0)
        aura_saved = round(p1.la_float(27), 2)
        _, s_aura = dpad_save(emu)

        def spend_aura():
            run_pad_script(emu, [
                _pad_step(buttons=("B",), stick=DOWN, hold=8),
                _pad_step(buttons=("A",), stick=NEUTRAL, hold=6),
            ])
            wait_idle(emu, p1, 120)

        spend_aura()
        aura_spend = round(p1.la_float(27), 2)
        _, l_aura = dpad_load(emu)
        aura_load = round(p1.la_float(27), 2)
        spend_aura()
        aura_load_spend = round(p1.la_float(27), 2)
        if aura_saved - aura_spend < 55.0:
            fails.append(f"lucario aura spend did not consume 60: {aura_saved} -> {aura_spend}")
        if abs(aura_load - aura_saved) > 0.1 or aura_load - aura_load_spend < 55.0:
            fails.append(f"lucario aura load: saved={aura_saved} load={aura_load} spend2={aura_load_spend}")
        notes.append(
            f"lucario aura: LA-Float[19] (damage mul) at 0%/100%={aura_mul_0}/{aura_mul_100}; "
            f"P+ LA-Float[27] saved={aura_saved} spend={aura_spend} load={aura_load} spend2={aura_load_spend} "
            f"(no-charge revert={reverted_aura} vs {base_aura})"
        )
        notes.append(f"  raw: {s_aura} | {l_aura}")
    return fails


def sc_donkey_kong(emu, notes):
    _, p1, cpu = prepare_match(emu, "donkey_kong")
    fails = []
    idle_snd = p1.status_sound_handle()
    place_cpu(emu, p1, cpu, close_dist=10.0)

    def dk_two_tap():
        """Tap B to enter windup, then tap B again to throw an uncharged or partial punch."""
        before = cpu.damage()
        run_pad_script(emu, [
            _pad_step(buttons=("B",), hold=3),
            _pad_step(hold=12),
            _pad_step(buttons=("B",), hold=4),
        ])
        t0 = nav.game_frame(emu)
        while nav.game_frame(emu) - t0 < 70:
            if cpu.damage() > before + 0.01:
                wait_game_frames(emu, 8)
                break
            emu.advance(2)
        wait_idle(emu, p1, 120)
        return round(cpu.damage() - before, 2)

    # (c) No-charge save & load.
    _, _ = dpad_save(emu)
    emu.hold(0, buttons=["B"])
    wait_cond(emu, "DK partial >= 4", lambda: p1.la_int(64) >= 4, timeout=160)
    emu.hold(0, buttons=["R"])
    wait_game_frames(emu, 6)
    emu.neutral(0)
    wait_idle(emu, p1, 120)
    pre_no = p1.la_int(64)
    _, _ = dpad_load(emu)
    after_no = p1.la_int(64)
    base_dmg = dk_two_tap()
    if pre_no < 4 or after_no != 0 or base_dmg <= 0.0:
        fails.append(f"no-charge load: pre={pre_no} after={after_no} base_dmg={base_dmg}")

    # Partial charge (5..8).
    emu.hold(0, buttons=["B"])
    wait_cond(emu, "DK partial >= 5", lambda: p1.la_int(64) >= 5, timeout=160)
    emu.hold(0, buttons=["R"])
    wait_game_frames(emu, 6)
    emu.neutral(0)
    wait_idle(emu, p1, 120)
    place_cpu(emu, p1, cpu, close_dist=10.0)
    part_saved = p1.la_int(64)
    _, _ = dpad_save(emu)
    part_fire_frame = nav.game_frame(emu)
    part_dmg = dk_two_tap()
    part_spend = p1.la_int(64)
    _, _ = dpad_load(emu)
    part_load = p1.la_int(64)
    wait_until_frame(emu, part_fire_frame)
    part_load_dmg = dk_two_tap()
    if not (0 < part_saved < 10) or part_spend != 0 or part_load != part_saved:
        fails.append(f"partial: saved={part_saved} spend={part_spend} load={part_load}")
    if not (part_dmg > base_dmg + 0.5 and abs(part_load_dmg - part_dmg) < 0.1):
        fails.append(f"partial dmg: base={base_dmg} spend={part_dmg} load={part_load_dmg}")

    # Full charge (10).
    emu.hold(0, buttons=["B"])
    wait_cond(emu, "DK full == 10", lambda: p1.la_int(64) >= 10, timeout=260)
    emu.neutral(0)
    wait_idle(emu, p1, 120)
    place_cpu(emu, p1, cpu, close_dist=10.0)
    full_saved = p1.la_int(64)
    _, s_full = dpad_save(emu)
    full_fire_frame = nav.game_frame(emu)
    full_dmg = fire_and_measure_damage(emu, p1, cpu, buttons=("B",), hold_f=3, wait_f=70)
    full_spend = p1.la_int(64)
    _, l_full = dpad_load(emu)
    full_load = p1.la_int(64)
    wait_until_frame(emu, full_fire_frame)
    full_load_dmg = fire_and_measure_damage(emu, p1, cpu, buttons=("B",), hold_f=3, wait_f=70)
    if full_saved != 10 or full_spend != 0 or full_load != 10:
        fails.append(f"full: saved={full_saved} spend={full_spend} load={full_load}")
    if not (full_dmg > part_dmg + 0.5 and abs(full_load_dmg - full_dmg) < 0.1):
        fails.append(f"full dmg: part={part_dmg} spend={full_dmg} load={full_load_dmg}")

    # Mid-charge save while winding up (hold B to wind; on load release B so DK
    # keeps winding to 10 without firing the punch).
    pos_mid = len(emu.synced_log_text())
    emu.hold(0, buttons=["B"])
    wait_cond(emu, "DK mid >= 3", lambda: p1.la_int(64) >= 3, timeout=180)
    _, _ = dpad_save(emu, extra=["B"], release_all=False)
    mid_read_frame = nav.game_frame(emu)
    mid_saved = p1.la_int(64)
    emu.hold(0, buttons=["R"])
    wait_game_frames(emu, 6)
    emu.neutral(0)
    wait_idle(emu, p1, 120)
    fire_and_measure_damage(emu, p1, cpu, buttons=("B",), hold_f=3, wait_f=60)
    wait_game_frames(emu, 20)
    _, _ = dpad_load(emu)
    wait_until_frame(emu, mid_read_frame)
    mid_at_load = p1.la_int(64)
    try:
        wait_cond(emu, "DK mid continue to 10", lambda: p1.la_int(64) >= 10, timeout=450)
    except Timeout:
        raise ChargeError(
            f"DK mid continue timed out: saved={mid_saved} at_load={mid_at_load} cur={p1.la_int(64)} status={p1.status():#x}"
        )
    wait_idle(emu, p1, 120)
    arts, snd = check_clean_idle(p1, idle_snd, emu.synced_log_text()[pos_mid:], fails, "dk mid-charge")
    if mid_at_load < mid_saved:
        fails.append(f"mid: saved={mid_saved} loaded={mid_at_load}")

    notes.append(
        f"donkey_kong: LA-Basic[64] partial {part_saved}->{part_spend}->{part_load} (dmg {base_dmg}/{part_dmg}/{part_load_dmg}); "
        f"full {full_saved}->{full_spend}->{full_load} (dmg {full_dmg}/{full_load_dmg}); no-charge={after_no}; "
        f"mid saved={mid_saved} loaded={mid_at_load} arts={arts} snd={snd:#x}"
    )
    notes.append(f"  raw: {s_full} | {l_full}")
    return fails


def sc_ivysaur(emu, notes):
    _, p1, cpu = prepare_match(emu, "ivysaur")
    fails = []
    idle_snd = p1.status_sound_handle()
    place_cpu(emu, p1, cpu, close_dist=10.0)

    # (c) No-charge save & load. Synthesis (Neutral-B while LA-Float[27] < 26)
    # adds +1.0 to LA-Float[27] every 36 frames.
    _, _ = dpad_save(emu)
    emu.hold(0, buttons=["B"])
    wait_cond(emu, "Ivysaur Synthesis >= 3", lambda: p1.la_float(27) >= 3.0, timeout=220)
    emu.neutral(0)
    wait_idle(emu, p1, 120)
    pre_no = round(p1.la_float(27), 2)
    _, _ = dpad_load(emu)
    after_no = round(p1.la_float(27), 2)
    if pre_no < 3.0 or after_no != 0.0:
        fails.append(f"no-charge load: pre={pre_no} after={after_no}")

    # Full Solarbeam (>= 26.0): build with Up-Smashes on CPU + Neutral-B Synthesis, save, fire Neutral-B, load, fire again.
    for _ in range(6):
        if p1.la_float(27) >= 26.0:
            break
        place_cpu(emu, p1, cpu, close_dist=8.0)
        fire_and_measure_damage(emu, p1, cpu, buttons=(), c=UP, hold_f=3, wait_f=55)
    if p1.la_float(27) < 26.0:
        emu.hold(0, buttons=["B"])
        wait_cond(emu, "Ivysaur Solarbeam >= 26", lambda: p1.la_float(27) >= 26.0, timeout=1100)
        emu.neutral(0)
        wait_idle(emu, p1, 140)
    place_cpu(emu, p1, cpu, close_dist=8.0)
    full_saved = round(p1.la_float(27), 2)
    _, s_full = dpad_save(emu)
    fire_frame = nav.game_frame(emu)
    full_dmg = fire_and_measure_damage(emu, p1, cpu, buttons=("B",), hold_f=4, wait_f=80)
    full_spend = round(p1.la_float(27), 2)
    _, l_full = dpad_load(emu)
    full_load = round(p1.la_float(27), 2)
    wait_until_frame(emu, fire_frame)  # match pre-spend frame offset for multi-hit Solarbeam
    full_load_dmg = fire_and_measure_damage(emu, p1, cpu, buttons=("B",), hold_f=4, wait_f=80)
    if full_saved < 26.0 or full_spend != 0.0 or abs(full_load - full_saved) > 0.1:
        fails.append(f"full: saved={full_saved} spend={full_spend} load={full_load}")
    if full_dmg < 10.0 or abs(full_load_dmg - full_dmg) >= 0.1:
        fails.append(f"full dmg: spend={full_dmg} load={full_load_dmg}")

    # Mid-charge save while holding Neutral-B (Synthesis).
    pos_mid = len(emu.synced_log_text())
    emu.hold(0, buttons=["B"])
    wait_cond(emu, "Ivysaur mid >= 2", lambda: p1.la_float(27) >= 2.0, timeout=180)
    _, _ = dpad_save(emu, extra=["B"], release_all=False)
    mid_read_frame = nav.game_frame(emu)
    mid_saved = round(p1.la_float(27), 2)
    emu.neutral(0)
    wait_idle(emu, p1, 120)
    wait_game_frames(emu, 36)
    _, _ = dpad_load(emu, extra=["B"], release_all=False)
    wait_until_frame(emu, mid_read_frame)
    mid_at_load = round(p1.la_float(27), 2)
    wait_cond(emu, "Ivysaur mid continue >= mid+2", lambda: p1.la_float(27) >= mid_saved + 2.0, timeout=220)
    emu.neutral(0)
    wait_idle(emu, p1, 140)
    arts, snd = check_clean_idle(p1, idle_snd, emu.synced_log_text()[pos_mid:], fails, "ivysaur mid-charge")
    if mid_at_load < mid_saved - 1.5:
        fails.append(f"mid: saved={mid_saved} loaded={mid_at_load}")

    notes.append(
        f"ivysaur: LA-Float[27] full {full_saved}->{full_spend}->{full_load} (beam dmg {full_dmg}/{full_load_dmg}); "
        f"no-charge={after_no}; mid saved={mid_saved} loaded={mid_at_load} arts={arts} snd={snd:#x}"
    )
    notes.append(f"  raw: {s_full} | {l_full}")
    return fails


def sc_wario(emu, notes):
    _, p1, cpu = prepare_match(emu, "wario")
    fails = []
    place_cpu(emu, p1, cpu, close_dist=10.0)

    # Save early timer, let it tick ~120 frames, load early -> timer reverts.
    # Sample pre- and post-load at the same frame offset from save so kSaveHold
    # and async log flush jitter do not add timer drift.
    _, _ = dpad_save(emu)
    f_early_read = nav.game_frame(emu)
    t_early = p1.la_int(65)
    wait_game_frames(emu, 120)
    t_later = p1.la_int(65)
    _, _ = dpad_load(emu)
    wait_until_frame(emu, f_early_read)
    t_reverted = p1.la_int(65)
    if t_later < t_early + 90 or abs(t_reverted - t_early) > 25:
        fails.append(f"early revert: early={t_early} later={t_later} reverted={t_reverted}")

    # Let timer build, save, spend with Down-B Waft -> 0, load -> restored & keeps ticking.
    wait_game_frames(emu, 150)
    _, s_waft = dpad_save(emu)
    f_waft_read = nav.game_frame(emu)
    t_saved = p1.la_int(65)
    stage_saved = p1.la_int(66)
    spend_dmg = fire_and_measure_damage(emu, p1, cpu, buttons=("B",), stick=DOWN, hold_f=3, wait_f=70)
    t_spend = p1.la_int(65)
    _, l_waft = dpad_load(emu)
    wait_until_frame(emu, f_waft_read)
    t_load = p1.la_int(65)
    wait_game_frames(emu, 30)
    t_ticked = p1.la_int(65)
    if t_spend > 40 or abs(t_load - t_saved) > 25 or t_ticked <= t_load + 15:
        fails.append(f"waft: saved={t_saved} spend={t_spend} load={t_load} ticked={t_ticked}")

    notes.append(
        f"wario: LA-Basic[65] timer saved={t_saved} (stage {stage_saved}) spend={t_spend} (dmg {spend_dmg}) "
        f"load={t_load} +30f={t_ticked}; early {t_early}->{t_later}->{t_reverted}"
    )
    notes.append(f"  raw: {s_waft} | {l_waft}")
    return fails


def sc_squirtle(emu, notes):
    _, p1, cpu = prepare_match(emu, "squirtle")
    fails = []
    idle_snd = p1.status_sound_handle()
    place_cpu(emu, p1, cpu, close_dist=12.0)

    # (c) No-charge save & load. Shield-cancelling while charging stores LA-Basic[64]
    # (releasing B early fires instead of storing).
    _, _ = dpad_save(emu)
    emu.hold(0, buttons=["B"])
    wait_cond(emu, "Squirtle Water Gun >= 20", lambda: p1.la_int(64) >= 20, timeout=140)
    emu.hold(0, buttons=["B", "R"])
    wait_game_frames(emu, 6)
    emu.neutral(0)
    wait_idle(emu, p1, 120)
    pre_no = p1.la_int(64)
    _, _ = dpad_load(emu)
    after_no = p1.la_int(64)
    if pre_no < 20 or after_no != 0:
        fails.append(f"no-charge load: pre={pre_no} after={after_no}")

    # Full charge: hold B until Squirtle auto-completes back to Wait with max LA-Basic[64].
    emu.hold(0, buttons=["B"])
    wait_cond(emu, "Squirtle Water Gun >= 30", lambda: p1.la_int(64) >= 30, timeout=160)
    wait_cond(emu, "Squirtle Water Gun auto-complete to Wait", lambda: p1.status() == ST_WAIT, timeout=180)
    emu.neutral(0)
    wait_idle(emu, p1, 60)
    def fire_water_gun():
        """Fire stored Water Gun with a frame-exact B tap, wait for Squirtle
        and all water droplets to finish and for CPU's slide to settle, and
        return signed displacement in Squirtle's facing direction."""
        lr = 1.0 if p1.posture()["lr"] >= 0 else -1.0
        x_before = cpu.posture()["x"]
        run_pad_script(emu, [_pad_step(buttons=("B",), hold=4)])
        wait_cond(
            emu,
            "Squirtle Water Gun spend",
            lambda: p1.la_int(64) == 0 or p1.status() != ST_WAIT,
            timeout=30,
            step=1,
        )
        wait_idle(emu, p1, 120)
        wait_cond(emu, "Squirtle Water Gun droplets done", lambda: p1.active_articles() == 0, timeout=60, step=2)
        wait_x_settled(emu, cpu, 60)
        return round((cpu.posture()["x"] - x_before) * lr, 2)

    place_cpu(emu, p1, cpu, close_dist=12.0)
    full_saved = p1.la_int(64)
    _, s_full = dpad_save(emu)
    fire_frame = nav.game_frame(emu)
    # Spend Water Gun (pushes CPU; verify displacement and LA-Basic[64] -> 0).
    push1 = fire_water_gun()
    full_spend = p1.la_int(64)
    _, l_full = dpad_load(emu)
    full_load = p1.la_int(64)
    # Match the pre-spend frame offset from save so RNG and Wait idle phase are
    # identical for push1 and push2 instead of diverging (~50f vs ~4f).
    wait_until_frame(emu, fire_frame)
    push2 = fire_water_gun()
    if full_saved < 40 or full_spend != 0 or full_load != full_saved:
        fails.append(f"full: saved={full_saved} spend={full_spend} load={full_load}")
    if push1 < 10.0 or push2 < 10.0:
        fails.append(f"water gun push: spend={push1} after_load={push2}")

    # Mid-charge save while holding B.
    pos_mid = len(emu.synced_log_text())
    emu.hold(0, buttons=["B"])
    wait_cond(emu, "Squirtle mid >= 15", lambda: p1.la_int(64) >= 15, timeout=120)
    _, _ = dpad_save(emu, extra=["B"], release_all=False)
    mid_read_frame = nav.game_frame(emu)
    mid_saved = p1.la_int(64)
    emu.hold(0, buttons=["B", "R"])
    wait_game_frames(emu, 6)
    emu.neutral(0)
    wait_idle(emu, p1, 120)
    wait_game_frames(emu, 36)
    _, _ = dpad_load(emu, extra=["B"], release_all=False)
    wait_until_frame(emu, mid_read_frame)
    mid_at_load = p1.la_int(64)
    wait_cond(emu, "Squirtle mid continue to Wait", lambda: p1.la_int(64) >= full_saved and p1.status() == ST_WAIT, timeout=180)
    emu.neutral(0)
    wait_idle(emu, p1, 60)
    arts, snd = check_clean_idle(p1, idle_snd, emu.synced_log_text()[pos_mid:], fails, "squirtle mid-charge")
    if mid_at_load < mid_saved - 2:
        fails.append(f"mid: saved={mid_saved} loaded={mid_at_load}")

    notes.append(
        f"squirtle: LA-Basic[64] full {full_saved}->{full_spend}->{full_load} (push dx {push1}/{push2}); "
        f"no-charge={after_no}; mid saved={mid_saved} loaded={mid_at_load} arts={arts} snd={snd:#x}"
    )
    notes.append(f"  raw: {s_full} | {l_full}")
    return fails


def sc_rob(emu, notes):
    _, p1, cpu = prepare_match(emu, "rob")
    fails = []
    idle_snd = p1.status_sound_handle()
    place_cpu(emu, p1, cpu)

    # (c) No-charge Gyro save & load (LA-Float[20]).
    _, _ = dpad_save(emu)
    emu.hold(0, buttons=["B"], stick=DOWN)
    wait_cond(emu, "ROB Gyro >= 20", lambda: p1.la_float(20) >= 20.0, timeout=160)
    emu.hold(0, buttons=["R"])
    wait_game_frames(emu, 6)
    emu.neutral(0)
    wait_idle(emu, p1, 120)
    pre_no = round(p1.la_float(20), 2)
    _, _ = dpad_load(emu)
    after_no = round(p1.la_float(20), 2)
    base_dmg = fire_and_measure_damage(emu, p1, cpu, buttons=("B",), stick=DOWN, hold_f=3, wait_f=70)
    if pre_no < 20.0 or after_no != 0.0:
        fails.append(f"no-charge load: pre={pre_no} after={after_no}")

    # Load no-charge state so no spinning Gyro lingers on stage, then test mid-charge Gyro save.
    _, _ = dpad_load(emu)
    pos_mid = len(emu.synced_log_text())
    emu.hold(0, buttons=["B"], stick=DOWN)
    wait_cond(emu, "ROB Gyro mid >= 20", lambda: p1.la_float(20) >= 20.0, timeout=160)
    mid_arts = p1.active_articles()
    _, _ = dpad_save(emu, extra=["B"], stick=DOWN, release_all=False)
    mid_read_frame = nav.game_frame(emu)
    mid_saved = round(p1.la_float(20), 2)
    emu.hold(0, buttons=["R"])
    wait_game_frames(emu, 6)
    emu.neutral(0)
    wait_idle(emu, p1, 120)
    wait_game_frames(emu, 36)
    _, _ = dpad_load(emu, extra=["B"], stick=DOWN, release_all=False)
    wait_until_frame(emu, mid_read_frame)
    mid_at_load = round(p1.la_float(20), 2)
    wait_cond(emu, "ROB Gyro mid continue >= 60", lambda: p1.la_float(20) >= 60.0, timeout=220)
    emu.neutral(0)
    wait_idle(emu, p1, 140)
    arts, snd = check_clean_idle(p1, idle_snd, emu.synced_log_text()[pos_mid:], fails, "rob mid-charge")
    if mid_arts < 1 or mid_at_load < mid_saved - 2.0:
        fails.append(f"mid: arts={mid_arts} saved={mid_saved} loaded={mid_at_load}")

    # Full Gyro charge (already >= 60 from mid-continue): save, fire Gyro + Robo Beam, load, fire Gyro again.
    place_cpu(emu, p1, cpu)
    _, s_full = dpad_save(emu)
    f_rob_read = nav.game_frame(emu)
    full_saved = round(p1.la_float(20), 2)
    beam_saved = round(p1.la_float(19), 2)
    full_dmg = fire_and_measure_damage(emu, p1, cpu, buttons=("B",), stick=DOWN, hold_f=3, wait_f=70)
    full_spend = round(p1.la_float(20), 2)
    fire_and_measure_damage(emu, p1, cpu, buttons=("B",), hold_f=3, wait_f=50)
    beam_spend = round(p1.la_float(19), 2)
    _, l_full = dpad_load(emu)
    wait_until_frame(emu, f_rob_read)
    full_load = round(p1.la_float(20), 2)
    beam_load = round(p1.la_float(19), 2)
    full_load_dmg = fire_and_measure_damage(emu, p1, cpu, buttons=("B",), stick=DOWN, hold_f=3, wait_f=70)
    if full_saved < 60.0 or full_spend != 0.0 or abs(full_load - full_saved) > 0.1:
        fails.append(f"gyro full: saved={full_saved} spend={full_spend} load={full_load}")
    if not (full_dmg > base_dmg + 0.5 and abs(full_load_dmg - full_dmg) < 0.1):
        fails.append(f"gyro dmg: base={base_dmg} spend={full_dmg} load={full_load_dmg}")
    if beam_spend >= beam_saved - 50.0 or abs(beam_load - beam_saved) > 25.0:
        fails.append(f"beam timer: saved={beam_saved} spend={beam_spend} load={beam_load}")

    notes.append(
        f"rob: Gyro LA-Float[20] {full_saved}->{full_spend}->{full_load} (dmg {base_dmg}/{full_dmg}/{full_load_dmg}); "
        f"Beam LA-Float[19] {beam_saved}->{beam_spend}->{beam_load}; no-charge={after_no}; "
        f"mid saved={mid_saved} (arts={mid_arts}) loaded={mid_at_load} clean arts={arts} snd={snd:#x}"
    )
    notes.append(f"  raw: {s_full} | {l_full}")
    return fails


def sc_lite(emu, notes):
    """Step 4: Three CPUs (4 fighters) forces the Recording page to the lite
    start state. Verify position, facing, damage and shield restore while
    Samus's Charge Shot (LA-Basic[65]) is lost."""
    lab, p1, cpu = prepare_match(emu, "samus")
    fails = []
    nav.set_training_cpus(emu, 3)
    wait_cond(emu, "4 fighters after raising CPUs to 3", lambda: len(LT.Fighters(emu).entries()) == 4, timeout=300)
    wait_game_frames(emu, 30)
    wait_idle(emu, p1, 120)

    # Charge Samus fully, move right, set P1 damage to 37%, save lite positions.
    emu.hold(0, buttons=["B"])
    wait_cond(emu, "Samus full charge in 3-CPU match", lambda: p1.la_int(65) >= 6, timeout=260)
    emu.neutral(0)
    wait_idle(emu, p1, 140)
    emu.hold(0, stick=RIGHT)
    wait_game_frames(emu, 18)
    emu.neutral(0)
    wait_idle(emu, p1, 120)
    set_p1_damage(emu, p1, 37.0)
    wait_game_frames(emu, 4)
    saved_pos = p1.posture()
    saved_dmg = round(p1.damage(), 1)
    saved_shd = round(p1.shield(), 1)
    saved_chg = p1.la_int(65)
    how_s, s_line = rec_save_positions(emu, lab)

    # Move left (turns facing to -1), change damage to 80%, spend charge shot.
    emu.hold(0, stick=LEFT)
    wait_game_frames(emu, 30)
    emu.neutral(0)
    wait_idle(emu, p1, 120)
    set_p1_damage(emu, p1, 80.0)
    run_pad_script(emu, [_pad_step(buttons=("B",), hold=3)])
    wait_idle(emu, p1, 120)
    moved_pos = p1.posture()
    spend_chg = p1.la_int(65)

    # Restore Positions (lite): position, facing, damage, shield restore; charge stays 0.
    how_r, r_line = rec_restore_positions(emu, lab)
    rest_pos = p1.posture()
    rest_dmg = round(p1.damage(), 1)
    rest_shd = round(p1.shield(), 1)
    rest_chg = p1.la_int(65)

    if not how_s.startswith("lite") or how_r != "lite":
        fails.append(f"expected lite fallback with 3 CPUs, got save={how_s!r} restore={how_r!r}")
    if abs(rest_pos["x"] - saved_pos["x"]) > 1.0 or rest_pos["lr"] != saved_pos["lr"]:
        fails.append(f"lite did not restore pos/lr: saved={saved_pos} moved={moved_pos} rest={rest_pos}")
    if abs(rest_dmg - saved_dmg) > 0.1 or abs(rest_shd - saved_shd) > 0.5:
        fails.append(f"lite did not restore dmg/shield: saved=({saved_dmg},{saved_shd}) rest=({rest_dmg},{rest_shd})")
    if spend_chg != 0 or rest_chg != 0:
        fails.append(f"lite charge: saved={saved_chg} spend={spend_chg} rest={rest_chg} (expected lost=0)")

    # Converse: re-save lite with 0 charge, charge up, restore -> charge is untouched (still > 0).
    how_s2, _ = rec_resave_positions(emu, lab)
    emu.hold(0, buttons=["B"])
    wait_cond(emu, "Samus partial charge after 0-charge lite save", lambda: p1.la_int(65) >= 3, timeout=180)
    emu.hold(0, buttons=["R"])
    wait_game_frames(emu, 6)
    emu.neutral(0)
    wait_idle(emu, p1, 120)
    chg_before_r2 = p1.la_int(65)
    how_r2, _ = rec_restore_positions(emu, lab)
    chg_after_r2 = p1.la_int(65)
    if chg_after_r2 != chg_before_r2 or chg_after_r2 < 3:
        fails.append(f"lite converse: expected charge untouched ({chg_before_r2}), got {chg_after_r2}")

    # Lower CPUs back to 1 before leaving.
    nav.set_training_cpus(emu, 1)
    wait_game_frames(emu, 20)

    notes.append(
        f"lite (3 CPUs): save={how_s} restore={how_r}; "
        f"x {saved_pos['x']:.1f}->{moved_pos['x']:.1f}->{rest_pos['x']:.1f}, "
        f"lr {saved_pos['lr']:.0f}->{moved_pos['lr']:.0f}->{rest_pos['lr']:.0f}, "
        f"dmg {saved_dmg}->80.0->{rest_dmg}, shield {saved_shd}->{rest_shd}; "
        f"LA-Basic[65] {saved_chg}->{spend_chg}->{rest_chg} (converse 0-save then charge {chg_before_r2}->{chg_after_r2})"
    )
    notes.append(f"  raw: {s_line} | {r_line}")
    return fails


SCENARIOS = {
    "lucas": sc_lucas,
    "samus": sc_samus,
    "mewtwo": sc_mewtwo,
    "lucario": sc_lucario,
    "donkey_kong": sc_donkey_kong,
    "ivysaur": sc_ivysaur,
    "wario": sc_wario,
    "squirtle": sc_squirtle,
    "rob": sc_rob,
    "lite": sc_lite,
}


def parse_args(rest):
    ap = argparse.ArgumentParser(prog="emu.py test-charges")
    ap.add_argument("rel")
    ap.add_argument("--only", nargs="+", choices=list(SCENARIOS))
    ap.add_argument("--keep-going", action="store_true")
    return ap.parse_args(rest)


def cmd_test_charges(rest):
    opts = parse_args(rest)
    label = "test-charges"
    E.cmd_install(opts.rel)
    backend = E.headless_backend()
    names = opts.only or list(SCENARIOS)
    results = []
    notes = []
    why, log_path = "", ""
    t0 = time.monotonic()
    try:
        with E.harness_run(backend, E.DETERMINISTIC_ARGS, allow_null=True) as (proc, emu):
            log_path = emu.log_path
            nav.boot_to_training_css(emu)
            for name in names:
                print(f"== {name}")
                t1 = time.monotonic()
                try:
                    fails = SCENARIOS[name](emu, notes)
                except (ChargeError, Timeout, nav.NavError, HarnessError) as e:
                    fails = [f"ERROR: {e}"]
                verdict = "PASS" if not fails else "FAIL"
                results.append((name, verdict, time.monotonic() - t1, fails))
                for f in fails:
                    print("    FAIL " + f)
                print(f"   ({time.monotonic() - t1:.0f} s) {verdict}")
                if fails and not opts.keep_going:
                    break
            if nav.scene(emu)[0] == "scMelee":
                nav.finish_training(emu)
            log = emu.log_text()
            bad = L.problems(log)
            print()
            for n in notes:
                print("  " + n)
            print()
            print(f"{'scenario':16s} {'time':>6s}  result  notes")
            for name, verdict, secs, fails in results:
                print(f"{name:16s} {secs:5.0f}s  {verdict:6s}  {fails[0] if fails else ''}")
            for line in bad:
                print(f"problem: {line}")
            print(f"time: {time.monotonic() - t0:.0f} s")
            if any(v != "PASS" for _, v, _, _ in results):
                if not bad and len(results) == len(names):
                    print("failed --only: " + " ".join(name for name, v, _, _ in results if v != "PASS"))
                why = "scenario failures"
            elif bad:
                why = "problem lines in the log"
    except (OSError, HarnessError, Timeout, nav.NavError, ValueError) as e:
        why = f"{e}"
    if why:
        return L.report(log_path, why, label)
    print(f"{label}: PASS")
    return 0
