"""Whole-memory dumps of the running game, and offline analysis of them.

usage: memdump.py heaps DUMP                 heap table with block counts
       memdump.py blocks DUMP HEAP [--all]   one heap's blocks (allocated only
                                             unless --all)
       memdump.py modules DUMP               loaded REL modules and sections
       memdump.py diff A B [--top N] [--set SPEC]
                                             what changed from dump A to B,
                                             per heap and per block
       memdump.py audit SAVE LOAD --set SPEC [--top N]
                                             pointer audit of a region set
       memdump.py segments DUMP --set SPEC   the set's segments and sizes

A dump is a pair of files PREFIX.mem1 (MEM1, 24 MiB from 0x80000000) and
PREFIX.mem2 (MEM2, 64 MiB from 0x90000000), taken through the harness with
capture() while the game is held at a known point; DUMP is that PREFIX.

Heaps are read from the game's own table (g_HeapInfos, 0x47 entries of
{name, pool, size, arena}) and each pool's block list. The pool object sits at
the start of its range (pool +0x4 == pool) and ends with the sentinel block
header at +0x60; blocks tile the rest of the range: every block starts with a 0x20-byte header whose +0x4 is the
block's size including the header; free blocks are also chained through
+0x8 from the pool's sentinel (pool +0x20), in address order. See
src/shared/game/heap.cpp for where each field was read from.

A region set (SPEC, a JSON file) names what a savestate would copy:
  {"alloc": [heap names: the pool object, allocated blocks whole and free
             block headers],
   "whole": [heap names: the pool's whole range],
   "blocks": {heap name: rule}  the pool object, free block headers and the
             allocated blocks the rule selects (see select_blocks),
   "rel": [section indices of loaded REL modules, e.g. [5, 6]],
   "rel_skip": [module ids never copied],
   "ranges": [[address, length, label], ...]  fixed ranges,
   "exclude": [[address, length, label], ...]  never copied}
Numbers may be written as "0x..." strings.
"""
import argparse
import bisect
import json
import os
import struct
import sys

sys.dont_write_bytecode = True  # no __pycache__ in the repo

MEM1_BASE, MEM1_SIZE = 0x80000000, 0x01800000
MEM2_BASE, MEM2_SIZE = 0x90000000, 0x04000000

HEAP_INFOS = 0x80494958  # g_HeapInfos (RSBE01_02 symbol), 0x47 x 16 bytes
HEAP_COUNT = 0x47
POOL_START, POOL_END, POOL_ALLOCATED, POOL_USED, POOL_SENTINEL = 0x4, 0x8, 0x14, 0x1C, 0x20
POOL_OBJECT = 0x80  # the pool object at the heap start, its sentinel block header (+0x60) included
BLOCK_SIZE, BLOCK_NEXT, BLOCK_HEADER = 0x4, 0x8, 0x20
MODULE_LIST = 0x800030C8  # OS module queue {head, tail}

# Named stretches of memory outside the heaps (RSBE01_02 DOL layout, from the
# decompilation's splits; the gaps between its sections are folded into the
# neighbouring name).
STATIC_AREAS = [
    (0x80000000, 0x80004000, "lowmem"),
    (0x80004000, 0x80406000, "dol.text"),
    (0x80406000, 0x80420680, "dol.rodata"),
    (0x80420680, 0x804948E8, "dol.data"),
    (0x804948E8, 0x8059C420, "dol.bss"),
    (0x8059C420, 0x8059FF50, "dol.sdata"),
    (0x8059FF50, 0x805A1480, "dol.sbss"),
    (0x805A1480, 0x805A4000, "dol.sdata2"),
    (0x805A4000, 0x805B5160, "stacks/arena-lo"),
]


def num(v):
    return int(v, 0) if isinstance(v, str) else int(v)


def is_ram(a):
    return MEM1_BASE <= a < MEM1_BASE + MEM1_SIZE or MEM2_BASE <= a < MEM2_BASE + MEM2_SIZE


# ---- capture ---------------------------------------------------------------

def capture(emu, prefix, chunk=8 << 20):
    """Read all of MEM1 and MEM2 through the harness into PREFIX.mem1/.mem2.
    The caller holds the game still (parked or paused) while this runs."""
    os.makedirs(os.path.dirname(os.path.abspath(prefix)), exist_ok=True)
    for base, size, ext in ((MEM1_BASE, MEM1_SIZE, ".mem1"), (MEM2_BASE, MEM2_SIZE, ".mem2")):
        with open(prefix + ext + ".part", "wb") as f:
            for off in range(0, size, chunk):
                f.write(emu.read(base + off, min(chunk, size - off)))
        os.replace(prefix + ext + ".part", prefix + ext)


def remove(prefix):
    for ext in (".mem1", ".mem2"):
        if os.path.exists(prefix + ext):
            os.remove(prefix + ext)


# ---- reading a dump ---------------------------------------------------------

class Block:
    __slots__ = ("heap", "addr", "size", "free")

    def __init__(self, heap, addr, size, free):
        self.heap, self.addr, self.size, self.free = heap, addr, size, free

    @property
    def end(self):
        return self.addr + self.size


class Heap:
    def __init__(self, hid, name, pool, size, arena, start, end):
        self.id, self.name, self.pool, self.size, self.arena = hid, name, pool, size, arena
        self.start, self.end = start, end
        self.blocks = []
        self.walk_error = None

    @property
    def allocated(self):
        return [b for b in self.blocks if not b.free]


class Dump:
    def __init__(self, prefix):
        self.prefix = prefix
        with open(prefix + ".mem1", "rb") as f:
            self.mem1 = f.read()
        with open(prefix + ".mem2", "rb") as f:
            self.mem2 = f.read()
        self._heaps = None
        self._index = None

    def buf(self, addr):
        """(buffer, offset) holding `addr`."""
        if MEM1_BASE <= addr < MEM1_BASE + MEM1_SIZE:
            return self.mem1, addr - MEM1_BASE
        if MEM2_BASE <= addr < MEM2_BASE + MEM2_SIZE:
            return self.mem2, addr - MEM2_BASE
        raise ValueError(f"{addr:#x} is not in MEM1 or MEM2")

    def read(self, addr, n):
        b, o = self.buf(addr)
        return b[o:o + n]

    def u32(self, addr):
        b, o = self.buf(addr)
        return struct.unpack_from(">I", b, o)[0]

    def u16(self, addr):
        b, o = self.buf(addr)
        return struct.unpack_from(">H", b, o)[0]

    def cstr(self, addr, limit=40):
        if not is_ram(addr):
            return None
        raw = self.read(addr, limit)
        end = raw.find(b"\0")
        text = raw[:end if end >= 0 else limit]
        if not text or any(c < 0x20 or c > 0x7E for c in text):
            return None
        return text.decode("ascii")

    # -- heaps --

    def heaps(self):
        if self._heaps is None:
            self._heaps = []
            for hid in range(HEAP_COUNT):
                name_p, pool, size, arena = struct.unpack(">IIII", self.read(HEAP_INFOS + 16 * hid, 16))
                if not is_ram(pool):
                    continue
                h = Heap(hid, self.cstr(name_p) or f"heap{hid}", pool, size, arena,
                         self.u32(pool + POOL_START), self.u32(pool + POOL_END))
                self._walk(h)
                self._heaps.append(h)
        return self._heaps

    def heap(self, name):
        for h in self.heaps():
            if h.name == name or str(h.id) == str(name):
                return h
        return None

    def _walk(self, h):
        """Fill h.blocks: every block from start to end, by size; free ones
        are the members of the free list."""
        free = set()
        sentinel = self.u32(h.pool + POOL_SENTINEL)
        if is_ram(sentinel):
            b = self.u32(sentinel + BLOCK_NEXT)
            while h.start <= b < h.end and b != sentinel and b not in free and len(free) < 100000:
                free.add(b)
                b = self.u32(b + BLOCK_NEXT)
        a = h.start + POOL_OBJECT
        while a < h.end:
            size = self.u32(a + BLOCK_SIZE)
            if size < BLOCK_HEADER or size % 0x20 or a + size > h.end:
                h.walk_error = f"bad block size {size:#x} at {a:#x}"
                break
            h.blocks.append(Block(h, a, size, a in free))
            a += size
        if h.walk_error is None and free - {b.addr for b in h.blocks if b.free}:
            h.walk_error = "free list members outside the walk"

    def blocks(self):
        """All blocks of all heaps, sorted by address."""
        if self._index is None:
            allb = sorted((b for h in self.heaps() for b in h.blocks), key=lambda b: b.addr)
            self._index = (allb, [b.addr for b in allb])
        return self._index[0]

    def block_at(self, addr):
        """The heap block containing `addr`, or None."""
        self.blocks()
        allb, starts = self._index
        i = bisect.bisect_right(starts, addr) - 1
        if i >= 0 and addr < allb[i].end:
            return allb[i]
        return None

    def heap_at(self, addr):
        for h in self.heaps():
            if h.start <= addr < h.end:
                return h
        return None

    def where(self, addr):
        """A short name for the place `addr` is in."""
        b = self.block_at(addr)
        if b is not None:
            return f"{b.heap.name}" + ("(free)" if b.free else "")
        h = self.heap_at(addr)
        if h is not None:
            return h.name
        for lo, hi, name in STATIC_AREAS:
            if lo <= addr < hi:
                return name
        return "mem1-other" if addr < MEM2_BASE else "mem2-other"

    # -- REL modules --

    def modules(self):
        """[(id, name, [(index, address, size, exec)])] from the OS module list."""
        out = []
        m = self.u32(MODULE_LIST)
        seen = set()
        while is_ram(m) and m not in seen and len(out) < 64:
            seen.add(m)
            mid, nsec, secinfo = self.u32(m), self.u32(m + 0xC), self.u32(m + 0x10)
            name_off, name_size = self.u32(m + 0x14), self.u32(m + 0x18)
            name = self.cstr(name_off, min(name_size or 40, 64)) if is_ram(name_off) else None
            secs = []
            if is_ram(secinfo) and nsec < 64:
                for i in range(nsec):
                    off, size = struct.unpack(">II", self.read(secinfo + 8 * i, 8))
                    if size and is_ram(off & ~1):
                        secs.append((i, off & ~1, size, bool(off & 1)))
            out.append((mid, name, secs))
            m = self.u32(m + 4)
        return out


# ---- region sets --------------------------------------------------------------

def load_spec(path):
    with open(path, encoding="utf-8") as f:
        return json.load(f)


ARCHIVE_MAGIC = bytes.fromhex("41524300")  # "ARC" and a NUL: loaded .pac/.pcs files (archives) start with it


def is_archive(dump, b):
    return dump.read(b.addr + BLOCK_HEADER, 4) == ARCHIVE_MAGIC


def select_blocks(dump, heap, rule):
    """Allocated blocks of `heap` picked by `rule`: {"no_archive": true}
    skips blocks holding a loaded archive file; {"max_size": n} keeps blocks
    of at most n bytes (header included)."""
    out = []
    for b in heap.allocated:
        if rule.get("no_archive") and is_archive(dump, b):
            continue
        if "max_size" in rule and b.size > num(rule["max_size"]):
            continue
        out.append(b)
    return out


def merge(ranges):
    """Sorted, coalesced [(lo, hi, label)]; adjacent labels are joined."""
    out = []
    for lo, hi, label in sorted(ranges):
        if out and lo <= out[-1][1]:
            plo, phi, plabel = out[-1]
            out[-1] = (plo, max(phi, hi), plabel if label == plabel else plabel)
        else:
            out.append((lo, hi, label))
    return out


def subtract(ranges, holes):
    out = []
    holes = sorted(holes)
    for lo, hi, label in ranges:
        cur = lo
        for hlo, hhi, _ in holes:
            if hhi <= cur or hlo >= hi:
                continue
            if hlo > cur:
                out.append((cur, hlo, label))
            cur = max(cur, hhi)
        if cur < hi:
            out.append((cur, hi, label))
    return out


def resolve(dump, spec):
    """The set's ranges in this dump: sorted, merged [(lo, hi, label)]."""
    rs = []
    for name in spec.get("alloc", []):
        h = dump.heap(name)
        if h is None:
            continue
        rs.append((h.pool, h.pool + POOL_OBJECT, h.name + ":pool"))
        for b in h.blocks:
            rs.append((b.addr, b.addr + (BLOCK_HEADER if b.free else b.size), h.name))
    for name in spec.get("whole", []):
        h = dump.heap(name)
        if h is not None:
            rs.append((h.pool, h.pool + POOL_OBJECT, h.name + ":pool"))
            rs.append((h.start, h.end, h.name))
    for name, rule in spec.get("blocks", {}).items():
        h = dump.heap(name)
        if h is None:
            continue
        rs.append((h.pool, h.pool + POOL_OBJECT, h.name + ":pool"))
        for b in h.blocks:
            if b.free:
                rs.append((b.addr, b.addr + BLOCK_HEADER, h.name))
        for b in select_blocks(dump, h, rule):
            rs.append((b.addr, b.end, h.name + ":sel"))
    sections = set(spec.get("rel", []))
    skip = {num(x) for x in spec.get("rel_skip", [])}
    for mid, name, secs in dump.modules():
        if mid in skip:
            continue
        for i, addr, size, ex in secs:
            if i in sections and not ex:
                rs.append((addr, addr + size, f"rel{mid}.{i}"))
    for a, n, label in spec.get("ranges", []):
        rs.append((num(a), num(a) + num(n), label))
    holes = [(num(a), num(a) + num(n), label) for a, n, label in spec.get("exclude", [])]
    return merge(subtract(merge(rs), holes))


def total(ranges):
    return sum(hi - lo for lo, hi, _ in ranges)


class RangeSet:
    """Membership test for sorted, disjoint [(lo, hi, label)]."""

    def __init__(self, ranges):
        self.ranges = ranges
        self.starts = [r[0] for r in ranges]

    def find(self, addr):
        i = bisect.bisect_right(self.starts, addr) - 1
        if i >= 0 and addr < self.ranges[i][1]:
            return self.ranges[i]
        return None

    def __contains__(self, addr):
        return self.find(addr) is not None


# ---- diff ---------------------------------------------------------------------

def changed_runs(a, b, base, granule=32, page=4096):
    """[(address, bytes changed)] per changed granule between two equal-size
    buffers (exact byte counts)."""
    out = []
    n = len(a)
    for p in range(0, n, page):
        if a[p:p + page] == b[p:p + page]:
            continue
        for g in range(p, min(p + page, n), granule):
            x, y = a[g:g + granule], b[g:g + granule]
            if x != y:
                diff = (int.from_bytes(x, "big") ^ int.from_bytes(y, "big")).to_bytes(len(x), "big")
                out.append((base + g, len(x) - diff.count(0)))
    return out


def diff(da, db):
    """Changed bytes between two dumps: [(granule address, bytes)]."""
    return (changed_runs(da.mem1, db.mem1, MEM1_BASE) + changed_runs(da.mem2, db.mem2, MEM2_BASE))


def cmd_diff(args):
    da, db = Dump(args.a), Dump(args.b)
    runs = diff(da, db)
    per_heap, per_block = {}, {}
    for addr, n in runs:
        blk = da.block_at(addr)
        key = da.where(addr)
        per_heap[key] = per_heap.get(key, 0) + n
        if blk is not None:
            per_block[blk.addr] = per_block.get(blk.addr, 0) + n
    print(f"changed bytes: {sum(n for _, n in runs)} in {len(runs)} granules of 32")
    print(f"{'place':28s} {'changed':>10s} {'blocks changed/alloc':>22s} {'bytes in changed blocks':>24s}")
    heaps = {h.name: h for h in da.heaps()}
    for key, n in sorted(per_heap.items(), key=lambda kv: -kv[1]):
        h = heaps.get(key)
        if h is not None:
            ch = [b for b in h.allocated if b.addr in per_block]
            print(f"{key:28s} {n:10d} {len(ch):>10d}/{len(h.allocated):<11d} {sum(b.size for b in ch):24d}")
        else:
            print(f"{key:28s} {n:10d}")
    print(f"\ntop {args.top} changed blocks (heap, address, size, changed bytes, first words):")
    for addr, n in sorted(per_block.items(), key=lambda kv: -kv[1])[:args.top]:
        b = da.block_at(addr)
        words = " ".join(f"{da.u32(b.addr + 0x20 + 4 * i):08x}" for i in range(4))
        print(f"  {b.heap.name:20s} {b.addr:08x} {b.size:#9x} {n:8d}  {words}")
    if args.set:
        rs = RangeSet(resolve(da, load_spec(args.set)))
        out = [(a, n) for a, n in runs if a not in rs and (a + 31) not in rs]
        by = {}
        for a, n in out:
            by[da.where(a)] = by.get(da.where(a), 0) + n
        print(f"\nchanged bytes outside the set: {sum(n for _, n in out)}")
        for k, n in sorted(by.items(), key=lambda kv: -kv[1]):
            print(f"  {k:28s} {n:10d}")
    return 0


# ---- pointer audit ---------------------------------------------------------------

def words(dump, lo, hi):
    """(address, value) of every aligned word in [lo, hi) whose value is a
    RAM address."""
    b, o = dump.buf(lo)
    n = (hi - lo) // 4
    vals = struct.unpack_from(f">{n}I", b, o)
    for i, v in enumerate(vals):
        if (0x80000000 <= v < 0x81800000) or (0x90000000 <= v < 0x94000000):
            yield lo + 4 * i, v


def live_sources(dump):
    """Where pointers that matter can live: allocated blocks (headers
    included), static data, the low arena; not code, not free space."""
    src = [(b.addr, b.end, b.heap.name) for b in dump.blocks() if not b.free]
    for lo, hi, name in STATIC_AREAS:
        if name not in ("dol.text", "dol.rodata"):
            src.append((lo, hi, name))
    return merge(src)


def pointers(dump, rset, inside):
    """Pointers crossing the set's boundary: inside=False gives words
    outside the set pointing into it (inbound), True words inside pointing
    out (outbound) at allocated blocks or static data. {src: value}."""
    out = {}
    sources = rset.ranges if inside else subtract(live_sources(dump), rset.ranges)
    for lo, hi, _ in sources:
        lo, hi = (lo + 3) & ~3, hi & ~3
        if hi <= lo:
            continue
        # A range may cross from MEM1 into nothing; clip to its buffer.
        for addr, v in words(dump, lo, hi):
            if (v in rset) == inside:
                continue
            if inside:
                blk = dump.block_at(v)
                if blk is None:
                    if dump.heap_at(v) is not None:
                        continue  # into a heap's free space or header area
                    if not any(l <= v < h for l, h, n in STATIC_AREAS if n not in ("dol.text", "dol.rodata")):
                        continue
                elif blk.free:
                    continue
            out[addr] = v
    return out


# Places whose words look like pointers but are data in flight, not links a
# restore has to keep consistent: GPU command FIFO, frame buffers, sound
# sample data, thread stacks below the main loop. Counted, not listed.
TRANSIENT = ("RenderFifo", "CopyFB", "Sound", "stacks/arena-lo", "mem2-other", "System FW:thread")


def transient(dump, addr):
    w = dump.where(addr)
    if w in TRANSIENT:
        return True
    # OS thread stacks are System FW blocks filled with 0xdeadbabe when created.
    b = dump.block_at(addr)
    return b is not None and b.heap.name == "System FW" and dump.u32(b.addr + BLOCK_HEADER) == 0xDEADBABE


def window_blocks(ds, dl, rs_s):
    """Blocks of the set's heaps allocated or freed between the dumps."""
    names = {lab.split(":")[0] for _, _, lab in rs_s.ranges}
    out = []
    for h in ds.heaps():
        if h.name not in names:
            continue
        hl = dl.heap(h.name)
        a = {(b.addr, b.size) for b in h.allocated}
        b = {(x.addr, x.size) for x in hl.allocated} if hl else set()
        out += [(h.name, "freed", x) for x in sorted(a - b)] + [(h.name, "allocated", x) for x in sorted(b - a)]
    return out


def cmd_audit(args):
    ds, dl = Dump(args.save), Dump(args.load)
    spec = load_spec(args.set)
    rs_s, rs_l = RangeSet(resolve(ds, spec)), RangeSet(resolve(dl, spec))
    print(f"set: {total(rs_s.ranges)} bytes in {len(rs_s.ranges)} ranges at save, "
          f"{total(rs_l.ranges)} at load")
    changed = window_blocks(ds, dl, rs_s)
    print(f"blocks of the set's heaps allocated or freed between the dumps: {len(changed)}")
    for name, what, (addr, size) in changed[:args.top]:
        print(f"  {name:20s} {what:9s} {addr:08x} {size:#x}")
    # Inbound: words outside the set (at load time, which a restore leaves
    # as they are) pointing into it. Hazard when the word differs from the
    # save-time word, i.e. it no longer agrees with the restored contents.
    in_s, in_l = pointers(ds, rs_s, False), pointers(dl, rs_l, False)
    noise = [a for a in set(in_s) | set(in_l) if transient(ds, a)]
    for a in noise:
        in_s.pop(a, None)
        in_l.pop(a, None)
    print(f"(left out: {len(noise)} pointer-like words in transient places: {', '.join(TRANSIENT[:4])}, "
          f"stacks, OS thread stacks)")
    report(ds, dl, "inbound (outside -> set)", in_s, in_l, args.top)
    # Outbound: words inside the set (restored to their save-time value)
    # pointing outside. Hazard when the target block was freed or replaced.
    out_s = pointers(ds, rs_s, True)
    bad = {}
    for src, v in out_s.items():
        b_s, b_l = ds.block_at(v), dl.block_at(v)
        if b_s is None:
            continue  # static data: does not move
        if b_l is None or b_l.free or b_l.addr != b_s.addr or b_l.size != b_s.size:
            bad[src] = v
    print(f"\noutbound (set -> outside): {len(out_s)} pointers at save; "
          f"{len(bad)} point at a block freed or replaced by load time")
    summarize(ds, bad, args.top)
    return 0


def report(ds, dl, title, a, b, top):
    changed = {s: (a.get(s), b.get(s)) for s in set(a) | set(b) if a.get(s) != b.get(s)}
    print(f"\n{title}: {len(a)} pointers at save, {len(b)} at load; {len(changed)} differ")
    summarize(ds, {s: (v[0] if v[0] is not None else v[1]) for s, v in changed.items()}, top, dl, changed)


def summarize(ds, ptrs, top, dl=None, pairs=None):
    groups = {}
    for src, v in ptrs.items():
        key = (ds.where(src), ds.where(v))
        groups.setdefault(key, []).append(src)
    for (sw, tw), srcs in sorted(groups.items(), key=lambda kv: -len(kv[1]))[:top]:
        ex = sorted(srcs)[:3]
        if pairs is not None:
            exs = ", ".join(f"{s:08x}:{_fmt(pairs[s][0])}->{_fmt(pairs[s][1])}" for s in ex)
        else:
            exs = ", ".join(f"{s:08x}->{ptrs[s]:08x}" for s in ex)
        print(f"  {len(srcs):6d}  {sw:24s} -> {tw:24s}  e.g. {exs}")


def _fmt(v):
    return "none" if v is None else f"{v:08x}"


# ---- listings ---------------------------------------------------------------------

def magic(dump, b):
    raw = dump.read(b.addr + BLOCK_HEADER, 4)
    return raw.hex() if any(c < 0x20 or c > 0x7E for c in raw) else raw.decode("ascii")


def cmd_heaps(args):
    d = Dump(args.dump)
    print(f"{'id':>3s} {'name':22s} {'start':8s}-{'end':8s} {'size':>9s} {'alloc':>5s} {'bytes':>9s} "
          f"{'free':>9s} {'pool':8s} walk")
    for h in d.heaps():
        al = h.allocated
        free = sum(b.size for b in h.blocks if b.free)
        print(f"{h.id:3d} {h.name:22s} {h.start:08x}-{h.end:08x} {h.end - h.start:9x} {len(al):5d} "
              f"{sum(b.size for b in al):9x} {free:9x} {h.pool:08x} {h.walk_error or 'ok'}")
    return 0


def cmd_blocks(args):
    d = Dump(args.dump)
    h = d.heap(args.heap)
    if h is None:
        raise SystemExit(f"no heap {args.heap}")
    for b in h.blocks:
        if b.free and not args.all:
            continue
        hdr = " ".join(f"{d.u32(b.addr + 4 * i):08x}" for i in range(8))
        print(f"{b.addr:08x} {b.size:#9x} {'free' if b.free else 'used'} {magic(d, b):8s} hdr {hdr}")
    return 0


def cmd_modules(args):
    d = Dump(args.dump)
    for mid, name, secs in d.modules():
        print(f"module {mid} {name or ''}")
        for i, addr, size, ex in secs:
            print(f"   {i:2d} {addr:08x} {size:#9x} {'x' if ex else '-'} in {d.where(addr)}")
    return 0


def cmd_segments(args):
    d = Dump(args.dump)
    rs = resolve(d, load_spec(args.set))
    by = {}
    for lo, hi, label in rs:
        key = label.split(":")[0]
        by[key] = by.get(key, 0) + hi - lo
    for k, n in sorted(by.items(), key=lambda kv: -kv[1]):
        print(f"  {k:28s} {n:10d}")
    print(f"total {total(rs)} bytes in {len(rs)} ranges")
    return 0


def main():
    sys.stdout.reconfigure(line_buffering=True)
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("heaps")
    s.add_argument("dump")
    s = sub.add_parser("blocks")
    s.add_argument("dump")
    s.add_argument("heap")
    s.add_argument("--all", action="store_true")
    s = sub.add_parser("modules")
    s.add_argument("dump")
    s = sub.add_parser("diff")
    s.add_argument("a")
    s.add_argument("b")
    s.add_argument("--top", type=int, default=25)
    s.add_argument("--set")
    s = sub.add_parser("audit")
    s.add_argument("save")
    s.add_argument("load")
    s.add_argument("--set", required=True)
    s.add_argument("--top", type=int, default=25)
    s = sub.add_parser("segments")
    s.add_argument("dump")
    s.add_argument("--set", required=True)
    args = p.parse_args()
    return {"heaps": cmd_heaps, "blocks": cmd_blocks, "modules": cmd_modules, "diff": cmd_diff,
            "audit": cmd_audit, "segments": cmd_segments}[args.cmd](args)


if __name__ == "__main__":
    sys.exit(main())
