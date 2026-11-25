"""Frame-exact input for the emulator tests, played by the plugin itself.

The harness's own pad input (Emu.hold / Emu.press) reaches the game about a
frame after it is written, so a test cannot put an input on one exact game
frame with it (the first frame a technique is possible, for instance). The
plugin's script source (src/main/cpu/script_source.*) writes controls
straight into a fighter's input object from the fighter-input hook: a step
for driven frame N acts on game frame N, for a CPU or a human fighter alike.
It has two script blocks, so two fighters can be driven at once.

    inp = PluginInput(emu)
    mario = frames_to_steps({1: (B_JUMP, 0, 0), 6: (B_GUARD, 0.7, -0.7)}, 20)
    rows = inp.run({0: mario}, read=lambda i: {...})

`run` starts every script on the same game frame (driven frame 1), steps
the game one frame at a time until every step was played (plus `extra`
frames) and returns what `read(i)` gave after each frame i = 1, 2, ...
Buttons are aiInput bits (B_* below), sticks -1..1 with +y up. A script
ends with its last step; the fighter then gets its pad (or AI) back.

Timing-sensitive scenarios must use this and must include the earliest
possible timing of what they check (docs/testing.md).
"""
import re
import struct

# aiInput button bits (src/main/cpu/fighter_input.h).
B_ATTACK, B_SPECIAL, B_JUMP, B_GUARD, B_SMASH, B_FLICK = 0x1, 0x2, 0x4, 0x8, 0x20, 0x8000
B_GRAB = B_ATTACK | B_GUARD
MAGIC = 0x544D4344
STEPS = 96  # cpu::kScriptSteps
BLOCKS = 2  # cpu::kScriptBlocks
START_WAIT_FRAMES = 60  # frames run() waits for the game's first step
LOG = re.compile(r"cpu script: control block at ([0-9a-f]{8}) \((\d+) bytes\)")


class InputError(RuntimeError):
    pass


def frames_to_steps(frames, total, neutral=(0, 0.0, 0.0)):
    """{driven frame (1-based): (buttons, x, y)} -> script steps covering
    frames 1..total; frames not listed get `neutral`. Equal neighbours are
    merged into one step."""
    steps = []
    for n in range(1, total + 1):
        b, x, y = frames.get(n, neutral)
        if steps and steps[-1][:3] == (b, x, y):
            steps[-1] = (b, x, y, steps[-1][3] + 1)
        else:
            steps.append((b, x, y, 1))
    if len(steps) > STEPS:
        raise InputError(f"{len(steps)} steps, a script holds {STEPS}")
    return steps


class Block:
    """One of the module's script blocks (cpu::ScriptBlock)."""
    STEP = struct.Struct(">IffI")

    def __init__(self, emu, addr):
        self.emu = emu
        self.addr = addr
        if emu.u32(addr) != MAGIC:
            raise InputError(f"no script block at {addr:#x}")

    def play(self, player, steps, loop=False, also_sub=False, ai_off=False):
        """Starts `steps` ((buttons, x, y, frames) each) for player `player`
        (0-based); it takes the fighter's input on the next game frame."""
        if not 0 < len(steps) <= STEPS:
            raise InputError(f"a script holds 1-{STEPS} steps, not {len(steps)}")
        body = b"".join(self.STEP.pack(b, x, y, n) for b, x, y, n in steps)
        self.emu.h.call("write_mem", addr=self.addr + 0x3C, hex=body.hex())
        generation = self.emu.u32(self.addr + 4) + 1
        head = struct.pack(">IiIIIII", generation, player, len(steps), int(loop), int(also_sub), int(ai_off), 0)
        self.emu.h.call("write_mem", addr=self.addr + 4, hex=head.hex())

    def stop(self):
        self.emu.h.call("write_mem", addr=self.addr + 8, hex="ffffffff")

    def status(self):
        raw = self.emu.read(self.addr + 0x1C, 0x1C)
        u = lambda o: struct.unpack_from(">I", raw, o)[0]  # noqa: E731
        f = lambda o: struct.unpack_from(">f", raw, o)[0]  # noqa: E731
        return {"driven": u(0), "step": u(4), "ai_buttons": u(0xC), "ai_x": f(0x10), "ai_y": f(0x14),
                "calls": u(0x18)}


class PluginInput:
    def __init__(self, emu):
        self.emu = emu
        found = LOG.findall(emu.log_text())
        if not found:
            raise InputError("no script block line in the log")
        base, size = int(found[-1][0], 16), int(found[-1][1])
        self.blocks = [Block(emu, base + k * size) for k in range(BLOCKS)]

    def stop(self):
        for b in self.blocks:
            b.stop()

    def run(self, scripts, read=None, extra=0, each=None, until=None, ai_off=True):
        """From a paused game, plays {player: steps} (at most BLOCKS
        players), all starting on the next game frame, one frame at a time
        until every script ended and `extra` more frames passed, or until
        until(i, rows) is true. Returns [read(i) for i = 1..] (None without
        `read`); each(i) runs before frame i. The AI is off for driven CPUs.
        Leaves the game paused."""
        if not 0 < len(scripts) <= BLOCKS:
            raise InputError(f"1-{BLOCKS} scripts at a time, not {len(scripts)}")
        self.emu.h.call("pause")
        totals = {}
        for block, (player, steps) in zip(self.blocks, scripts.items()):
            block.play(player, steps, ai_off=ai_off)
            totals[player] = (block, sum(s[3] for s in steps))
        longest = max(t for _, t in totals.values())
        rows = []

        def after(i):
            """What is read once frame i has been stepped."""
            for player, (block, total) in totals.items():
                if i == 1 or i == total:
                    driven = block.status()["driven"]
                    if driven != min(i, total):
                        raise InputError(f"player {player + 1}: {driven} script frames driven after {i}")
            return read(i) if read else None
        # Each frame and what is read after it go to the harness as one
        # batch (harness.Watch): the reads are those of the frame before.
        watch = self.emu.watch(after)
        for i in range(1, longest + extra + 1):
            if each:
                each(i)
            if i == 1:
                self.emu.h.call("frame_advance", n=1)
                # The scripts advance with the game's own steps. The game can
                # spend a frame without one (it loads a fighter that was just
                # added, for example): frame 1 is the first frame it steps.
                for _ in range(START_WAIT_FRAMES):
                    if any(block.status()["driven"] for block, _ in totals.values()):
                        break
                    self.emu.h.call("frame_advance", n=1)
                rows.append(watch.read(i))
            else:
                rows.append(self.emu.step(watch, i))
            if until and until(i, rows):
                break
        return rows
