"""Scripted control of one Dolphin instance through the Brawl Online Dolphin
build's automation harness (--harness-port).

The harness speaks one JSON object per line each way over TCP on 127.0.0.1:
a request {"id": n, "cmd": name, ...args} gets {"id": n, "ok": true,
"result": {...}} or {"id": n, "ok": false, "error": text}. The commands used
here are status, wait_frame, pause, frame_advance, pad_set, pad_clear,
pad_script, read_mem, screenshot and log_mark (see the fork's
docs/harness-protocol.md).

Time is counted in the game's own pad reads (`input_polls` in status), one per
game frame once Brawl's pad thread runs, so "N frames" below means N reads of
the controller by the game, independent of host speed.

Stepping a paused game and reading it after every frame is what most tests
do. The harness has no request that returns chosen memory for every frame of
a run, and none that waits for a memory condition, so the reads stay on this
side; what can be saved is the wait for one answer before the next request
is sent. Harness.batch sends a list of requests at once, and Emu.watch,
Emu.step and Emu.trace build on it: a frame and everything read after it
travel as one batch. See docs/testing.md, "Stepping and reading in one
batch".

Environment (the Makefile passes these on):
  HARNESS_BATCH=0    send every request on its own, as before batches
  HARNESS_AUDIT=1    read everything a batch returned once more, one request
                     at a time, and fail on any difference
  HARNESS_PROFILE=F  write one line per request into the file F
"""
import json
import os
import re
import select
import socket
import struct
import time


class HarnessError(RuntimeError):
    pass


# A batch gives up when the harness has sent nothing for this long (the
# socket's own limit for a single request).
SILENCE_S = 60.0
# Requests that change nothing in the emulator or the game.
READ_ONLY = {"read_mem", "read_u32", "hash_mem", "status", "ping", "pad_script_status"}


class Harness:
    """Line-delimited JSON client for --harness-port."""

    def __init__(self, port):
        self.port = port
        self.sock = None
        self.buf = b""
        self.next_id = 0
        # Whether emulation was last paused or resumed through this client.
        self.paused = False
        # Called with the command of every request that is not read-only.
        self.on_change = None
        # HARNESS_PROFILE=<file>: one line per request (see _note).
        self.profile_path = os.environ.get("HARNESS_PROFILE", "").strip()
        self.profile = [] if self.profile_path else None
        self.t0 = time.perf_counter()

    def connect(self, proc, timeout):
        t0 = time.monotonic()
        while time.monotonic() - t0 < timeout and proc.poll() is None:
            try:
                self.sock = socket.create_connection(("127.0.0.1", self.port), timeout=SILENCE_S)
                self.sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
                return True
            except OSError:
                time.sleep(0.1)
        return False

    def _note(self, start, cmd, size, at):
        """Profile line: start (seconds since this object was made),
        duration (seconds), command, its size (frames, bytes or, for a
        batch, requests) and the frame count the answer carried (for a
        batch, the frames it advanced)."""
        self.profile.append(f"{start - self.t0:.6f} {time.perf_counter() - start:.6f} {cmd} {size} {at}")

    def _sent(self, cmd):
        if cmd == "pause" or cmd == "frame_advance":
            self.paused = True
        elif cmd == "resume":
            self.paused = False
        if self.on_change is not None and cmd not in READ_ONLY:
            self.on_change(cmd)

    def call(self, cmd, **args):
        if self.sock is None:
            raise HarnessError("harness not connected")
        start = time.perf_counter()
        self.next_id += 1
        args.update(id=self.next_id, cmd=cmd)
        self.sock.sendall((json.dumps(args) + "\n").encode())
        while True:
            while b"\n" not in self.buf:
                data = self.sock.recv(1 << 20)
                if not data:
                    raise HarnessError("harness connection closed")
                self.buf += data
            line, self.buf = self.buf.split(b"\n", 1)
            reply = json.loads(line)
            if reply.get("id") == self.next_id:
                break
        if self.profile is not None:
            result = reply.get("result") or {}
            self._note(start, cmd, args.get("n", args.get("len", args.get("input_polls", ""))),
                       result.get("input_polls", result.get("frame", "")))
        if not reply.get("ok"):
            raise HarnessError(f"harness {cmd}: {reply.get('error')}")
        self._sent(cmd)
        return reply["result"]

    def batch(self, requests):
        """Send every (cmd, args) of `requests` at once and return their
        results in order. The harness answers a request before it reads the
        next one, so the emulator is asked exactly what it is asked when the
        same requests are sent one by one with call(); only the wait for
        each answer before the next request is gone. If a request fails, the
        ones after it have run all the same; the first failure is raised
        once every answer is in."""
        if self.sock is None:
            raise HarnessError("harness not connected")
        if not requests:
            return []
        start = time.perf_counter()
        first = self.next_id + 1
        lines = []
        for cmd, args in requests:
            self.next_id += 1
            lines.append(json.dumps(dict(args, id=self.next_id, cmd=cmd)))
        out = ("\n".join(lines) + "\n").encode()
        replies, sent = {}, 0
        # Sending and receiving go together: with a long batch both sides'
        # socket buffers would fill up while each side waits for the other.
        while len(replies) < len(requests):
            readable, writable, _ = select.select([self.sock], [self.sock] if sent < len(out) else [], [], SILENCE_S)
            if not readable and not writable:
                raise HarnessError(f"harness batch: no answer for {SILENCE_S:.0f}s ({len(replies)} of "
                                   f"{len(requests)} requests answered)")
            if writable:
                sent += self.sock.send(out[sent:sent + (1 << 16)])
            if readable:
                data = self.sock.recv(1 << 20)
                if not data:
                    raise HarnessError("harness connection closed")
                *done, self.buf = (self.buf + data).split(b"\n")
                for line in done:
                    reply = json.loads(line)
                    if isinstance(reply.get("id"), int) and first <= reply["id"] <= self.next_id:
                        replies[reply["id"]] = reply
        if self.profile is not None:
            self._note(start, "batch", len(requests),
                       sum(a.get("n", 1) for c, a in requests if c == "frame_advance"))
        for k, (cmd, _) in enumerate(requests):
            reply = replies[first + k]
            if not reply.get("ok"):
                raise HarnessError(f"harness {cmd}: {reply.get('error')} (request {k + 1} of {len(requests)} "
                                   f"sent together)")
            self._sent(cmd)
        return [replies[first + k]["result"] for k in range(len(requests))]

    def close(self):
        if self.sock:
            self.sock.close()
            self.sock = None
        if self.profile:
            with open(self.profile_path, "w", encoding="utf-8") as f:
                f.write("\n".join(self.profile) + "\n")
            self.profile = []


def rotated_log_path(log_path):
    """Where this Dolphin moves dolphin.log once it is full (about 25 MiB),
    starting a new one: dolphin.prev.log."""
    root, ext = os.path.splitext(log_path)
    return root + ".prev" + ext


def _size(path):
    try:
        return os.path.getsize(path)
    except OSError:
        return None


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


class Timeout(RuntimeError):
    """A wait ran out of frames; `what` says what was being waited for."""


# Wall-clock limits. Frame waits fail only when the game makes no progress:
# Dolphin runs at below-normal priority, so on a busy machine it is slow,
# not stuck.
WAIT_CHUNK_MS = 5000   # one wait_frame request
FRAME_STALL_S = 60.0   # no new game frame for this long: stuck or paused
BOOT_STALL_S = 300.0   # booting: no change in state or log for this long


def _pad(buttons=(), stick=(128, 128), cstick=(128, 128), l=0, r=0):
    pad = {"buttons": list(buttons), "main": list(stick), "c": list(cstick), "l": l, "r": r}
    # The game reads the analog triggers as well as the digital bits.
    if "L" in buttons and not l:
        pad["l"] = 255
    if "R" in buttons and not r:
        pad["r"] = 255
    return pad


def _on(name):
    return os.environ.get(name, "").strip() not in ("", "0")


class Watch:
    """A function that reads game memory through an Emu (read, u8, u16, u32,
    f32, ptr, cstr), with its reads sent to the harness as one batch.

    The first time, the function runs as it is written: each read is a
    request of its own, and the reads it makes are noted. From then on the
    noted reads are sent together (after the frame advance, with Emu.step)
    and the function runs on the answers. It may read something else this
    time (a pointer changed, a branch went the other way): that read is made
    on the spot, and the list is the function's reads of its latest run.
    While emulation is paused nothing in memory moves between the batch and
    such a read, so the function returns exactly what it returns with every
    read sent on its own. A request that can change something (a write, a
    pad, a frame) from inside the function drops the batch's answers: the
    reads after it are made on the spot too.

    The function should do nothing but read and return a value.
    """

    def __init__(self, emu, fn):
        self.emu = emu
        self.fn = fn
        self.plan = []  # (address, length) of every read of the latest run

    def read(self, *args):
        """The function's value now."""
        return self.emu._watched(self, args, ())


class _View:
    """The answers a Watch's function reads from while it runs."""

    def __init__(self, data):
        self.data = data    # (address, length) -> bytes, from the batch
        self.asked = []     # every read the function made, in order
        self.served = {}    # what it was given from the batch
        self.changed = False


class Emu:
    """Input, memory, waiting and screenshots for a running instance.

    Every wait is bounded in game frames and raises Timeout with a description
    of what it waited for; callers decide what evidence to save.
    """

    def __init__(self, harness, log_path, shots_dir, headless=False):
        self.h = harness
        self.log_path = log_path
        self.shots_dir = shots_dir
        self.headless = headless
        self.batching = os.environ.get("HARNESS_BATCH", "").strip() != "0"
        self.audit = _on("HARNESS_AUDIT")
        self.audited = [0, 0]  # batches and reads checked (HARNESS_AUDIT)
        self._view = None
        self._log_bytes, self._log_lines = 0, ""  # see log_text
        self._prev_size = _size(rotated_log_path(log_path))
        self._boot_log_prefix = None
        self._post_load_offset = 0
        self.css_cache = None

    # ---- time ----------------------------------------------------------

    def wait_running(self, proc, timeout_s, stall_s=BOOT_STALL_S):
        """Wait until emulation has started: Dolphin first packs the SD
        folder into an image, which takes minutes on a busy machine (tests
        run at below-normal priority). Gives up when Dolphin exits, when
        neither the emulator's state nor its log has changed for `stall_s`
        seconds, or after `timeout_s` in all."""
        t0 = last = time.monotonic()
        seen = None
        while time.monotonic() - t0 < timeout_s:
            if proc.poll() is not None:
                raise HarnessError(f"Dolphin exited with code {proc.returncode}")
            state = self.h.call("status")["state"]
            if state == "running":
                return
            try:
                size = os.path.getsize(self.log_path)
            except OSError:
                size = -1
            if (state, size) != seen:
                seen, last = (state, size), time.monotonic()
            elif time.monotonic() - last > stall_s:
                raise Timeout(f"emulation to start (state {state!r}, no progress for {stall_s:.0f}s)")
            time.sleep(0.1)
        raise Timeout(f"emulation to start (gave up after {timeout_s:.0f}s)")

    def polls(self):
        return int(self.h.call("status")["input_polls"])

    def wait_polls(self, target, stall_s=FRAME_STALL_S):
        """Wait until the game has read the controllers `target` times.
        Slowness is not failure: this gives up only when the count has not
        moved for `stall_s` seconds (the game is stuck, or emulation is
        paused), whatever the total time."""
        last_polls, last_t = self.polls(), time.monotonic()
        while True:
            try:
                reply = self.h.call("wait_frame", input_polls=target, timeout_ms=WAIT_CHUNK_MS)
                return int(reply["input_polls"])
            except HarnessError as e:
                if "timeout" not in str(e):
                    raise
            status = self.h.call("status")
            now = int(status["input_polls"])
            if now >= target:
                return now
            if now != last_polls:
                last_polls, last_t = now, time.monotonic()
            elif time.monotonic() - last_t > stall_s:
                raise Timeout(f"game frame {target} (stuck at {now} for {stall_s:.0f}s, "
                              f"emulation {status.get('state')!r})")

    def advance(self, frames=1):
        """Let `frames` game frames pass; returns the new frame count."""
        return self.wait_polls(self.polls() + frames)

    def wait_for(self, what, probe, timeout_frames, every=1):
        """Advance `every` frames at a time until probe() returns something
        truthy; returns it. Raises Timeout after `timeout_frames`."""
        start = self.polls()
        while True:
            value = probe()
            if value:
                return value
            if self.polls() - start > timeout_frames:
                raise Timeout(f"{what} (gave up after {timeout_frames} frames)")
            self.advance(every)

    # ---- stepping a paused game ----------------------------------------

    def watch(self, fn):
        """A Watch on `fn`: see the class."""
        return Watch(self, fn)

    def _on_change(self, cmd):
        view = self._view
        if view is not None:
            view.data = {}
            view.changed = True

    def _watched(self, watch, args, first):
        """Send `first` (requests) and then the watch's reads, as one batch,
        and run its function on the answers."""
        h = self.h
        if self._view is not None:
            # Called from inside another watch's function: that one's
            # answers serve, and its list notes the reads.
            for cmd, a in first:
                h.call(cmd, **a)
            return watch.fn(*args)
        if self.batching and watch.plan:
            results = h.batch(list(first) + [("read_mem", {"addr": a, "len": n}) for a, n in watch.plan])
            data = {key: bytes.fromhex(r["hex"]) for key, r in zip(watch.plan, results[len(first):])}
        else:
            for cmd, a in first:
                h.call(cmd, **a)
            data = {}
        view = self._view = _View(data)
        h.on_change = self._on_change
        try:
            value = watch.fn(*args)
        finally:
            self._view, h.on_change = None, None
        watch.plan = list(dict.fromkeys(view.asked))
        if self.audit and view.served and h.paused and not view.changed:
            # Every answer the function was given from the batch against a
            # read of its own, now: nothing has run in between.
            for (addr, length), given in view.served.items():
                now = self._read(addr, length)
                if now != given:
                    raise HarnessError(f"harness audit: {length} bytes at {addr:#x} read {given.hex()} in the "
                                       f"batch and {now.hex()} on their own")
            self.audited[0] += 1
            self.audited[1] += len(view.served)
        return value

    def step(self, watch=None, *args, frames=1):
        """Advance paused emulation by `frames` frames (frame_advance) and
        return watch.read(*args), the reads sent in the same batch as the
        advance. The harness stops the CPU at the end of the last frame's
        video field before it answers, and the game does not run again
        until the next advance: the value is the game's memory as that
        frame left it, the same as after a frame_advance request followed
        by the reads one by one."""
        first = [("frame_advance", {"n": frames})]
        if watch is None:
            self.h.call("frame_advance", n=frames)
            return None
        return self._watched(watch, args, first)

    def trace(self, frames, watch, each=None, until=None, first=0):
        """Step paused emulation one frame at a time, `frames` times; returns
        [watch.read(i) after frame i] for i = first, first + 1, ... One
        batch per frame. each(i, rows) runs before frame i with the rows so
        far (it may set pads or write memory); the run ends early after the
        frame for which until(i, rows) is true."""
        rows = []
        for i in range(first, first + frames):
            if each:
                each(i, rows)
            rows.append(self.step(watch, i))
            if until and until(i, rows):
                break
        return rows

    def advance_until(self, what, watch, test, limit):
        """Step paused emulation one frame at a time until test(value) is
        true for the watch's value after a frame; returns (frames stepped,
        value). Raises Timeout after `limit` frames. The harness cannot wait
        for a memory condition itself, so this is one batch per frame; the
        game stops on the very frame the condition first holds."""
        for n in range(1, limit + 1):
            value = self.step(watch)
            if test(value):
                return n, value
        raise Timeout(f"{what} (gave up after {limit} frames)")

    # ---- input ---------------------------------------------------------

    def hold(self, port=0, buttons=(), stick=(128, 128), cstick=(128, 128), l=0, r=0):
        """Present this pad state on `port` until changed."""
        self.h.call("pad_set", port=port, **_pad(buttons, stick, cstick, l, r))

    def neutral(self, port=0):
        self.hold(port)

    def release(self, port=0):
        """Give the port back to its configured controller."""
        self.h.call("pad_clear", port=port)

    def press(self, port=0, buttons=(), frames=3, after=3, stick=(128, 128)):
        """Press for exactly `frames` game frames, then neutral for `after`;
        returns once all of it has been read by the game."""
        self.neutral(port)
        steps = [dict(_pad(buttons, stick), hold=frames)]
        if after:
            steps.append(dict(_pad(), hold=after))
        result = self.h.call("pad_script", port=port, frames=steps, start="next")
        self.wait_polls(int(result["ends_at"]))

    # ---- memory --------------------------------------------------------

    def _read(self, addr, length):
        return bytes.fromhex(self.h.call("read_mem", addr=addr, len=length)["hex"])

    def read(self, addr, length):
        view = self._view
        if view is None:
            return self._read(addr, length)
        key = (addr, length)
        view.asked.append(key)
        data = view.data.get(key)
        if data is None:
            return self._read(addr, length)
        view.served[key] = data
        return data

    def u8(self, addr):
        return self.read(addr, 1)[0]

    def u16(self, addr):
        return struct.unpack(">H", self.read(addr, 2))[0]

    def u32(self, addr):
        return struct.unpack(">I", self.read(addr, 4))[0]

    def f32(self, addr):
        return struct.unpack(">f", self.read(addr, 4))[0]

    @staticmethod
    def is_ram(addr):
        return 0x80000000 <= addr < 0x81800000 or 0x90000000 <= addr < 0x94000000

    def ptr(self, addr):
        """The pointer stored at `addr`, or None if it does not point into RAM."""
        if not self.is_ram(addr):
            return None
        p = self.u32(addr)
        return p if self.is_ram(p) else None

    def cstr(self, addr, limit=40):
        if not self.is_ram(addr):
            return None
        raw = self.read(addr, limit)
        end = raw.find(b"\0")
        text = raw[:end if end >= 0 else limit]
        if not text or any(c < 0x20 or c > 0x7E for c in text):
            return None
        return text.decode("ascii")

    # ---- log and screenshots -------------------------------------------

    def log_text(self):
        """Dolphin's log so far. Dolphin only appends to it while it runs,
        so each call reads what is new and keeps the whole lines it has
        seen (tests ask for the log after every frame, and it grows to
        megabytes). When Dolphin moves a full log aside and starts a new
        one, the rest of the old file is read before the new one."""
        prev = rotated_log_path(self.log_path)
        size = _size(prev)
        if size is not None and size != self._prev_size:
            try:
                with open(prev, "rb") as f:
                    f.seek(self._log_bytes)
                    self._log_lines += f.read().decode("utf-8", "replace")
            except OSError:
                pass
            self._log_bytes, self._prev_size = 0, size
        try:
            with open(self.log_path, "rb") as f:
                f.seek(self._log_bytes)
                new = f.read()
        except OSError:
            return ""
        # A line break is never part of a longer character, so decoding
        # whole lines apart gives the same text as decoding the file at once.
        cut = new.rfind(b"\n") + 1
        if cut:
            self._log_lines += new[:cut].decode("utf-8", "replace")
            self._log_bytes += cut
        return self._log_lines + new[cut:].decode("utf-8", "replace")

    def mark(self, text):
        """Write `[HARNESS] text` into dolphin.log, to line log lines up with steps."""
        self.h.call("log_mark", text=text)

    def synced_log_text(self, timeout_s=60.0):
        """log_text() once every line logged before this call is in the file.
        Brawl Online's Dolphin writes dolphin.log from a thread of its own,
        some time after a line is logged; its lines keep their order, so a
        mark logged now is in the file only after all of them."""
        self._marks = getattr(self, "_marks", 0) + 1
        mark = f"log sync {self._marks}."
        start = len(self.log_text())
        self.mark(mark)
        deadline = time.monotonic() + timeout_s
        while True:
            text = self.log_text()
            if text.find("[HARNESS] " + mark, start) >= 0:
                return text
            if time.monotonic() > deadline:
                raise Timeout(f"dolphin.log to take the lines logged so far ({timeout_s:.0f}s)")
            time.sleep(0.02)

    def seed_boot_log(self, prefix):
        """Replace the log seen so far with `prefix` (the boot log captured
        with a cached CSS savestate), positioning the reader at the current
        flushed EOF so every line Dolphin writes after load_state appends
        cleanly after `prefix`."""
        self.synced_log_text()
        if prefix and not prefix.endswith("\n"):
            prefix = prefix + "\n"
        self._boot_log_prefix = prefix
        self._post_load_offset = self._log_bytes
        self._log_lines = prefix

    def clear_boot_log(self):
        """Drop any seeded boot log prefix (used on fallback to cold boot)."""
        self._boot_log_prefix = None
        self._post_load_offset = 0
        self._log_bytes = 0
        self._log_lines = ""

    def wait_log(self, what, regex, timeout_frames, every=10):
        rx = re.compile(regex)
        return self.wait_for(what, lambda: rx.search(self.log_text()), timeout_frames, every)

    def screenshot(self, name):
        """Save `name`.png in the screenshot folder; returns (path, width, height).
        Under the Null backend nothing is rendered to capture."""
        if self.headless:
            return f"(no screenshot under Null: {name})", 0, 0
        os.makedirs(self.shots_dir, exist_ok=True)
        path = os.path.join(self.shots_dir, name + ".png")
        if os.path.exists(path):
            os.remove(path)
        result = self.h.call("screenshot", path=path)
        return path, result["width"], result["height"]
