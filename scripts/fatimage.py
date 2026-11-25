"""Small FAT32 writer for the storage tests' own copy of the SD card image.

The storage tests run Dolphin on a copy of SD_IMAGE (see emu.py,
storage_image); this module puts files into that copy and takes them out
again before a boot: the plugin build, and hand-made files for the
migration and error cases. It never opens SD_IMAGE or SD_FOLDER for
writing (emu.py checks the path before calling it).

Files get a long name (VFAT) plus a generated 8.3 short name, as the game's
FAT library writes them. Both FAT copies and the FSInfo sector are kept up
to date. Reading uses sd-extract.py's reader.
"""
import importlib.util
import os
import struct
import sys

sys.dont_write_bytecode = True  # no __pycache__ in the repo

HERE = os.path.dirname(os.path.abspath(__file__))
_spec = importlib.util.spec_from_file_location("sd_extract", os.path.join(HERE, "sd-extract.py"))
sd_extract = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(sd_extract)

EOC = 0x0FFFFFFF
ATTR_DIR, ATTR_ARCHIVE, ATTR_LFN = 0x10, 0x20, 0x0F


def _lfn_checksum(short):
    s = 0
    for c in short:
        s = (((s & 1) << 7) + (s >> 1) + c) & 0xFF
    return s


class FatWriter(sd_extract.Fat):
    """sd_extract.Fat opened for writing (FAT32 only)."""

    def __init__(self, path):
        super().__init__(path)
        if not self.fat32:
            raise ValueError("only FAT32 images are written")
        self.f.close()
        self.f = open(path, "r+b")
        self.fat = bytearray(self.fat)
        bs = self._read(0, 512)
        self.fsinfo = struct.unpack_from("<H", bs, 48)[0]
        total = struct.unpack_from("<H", bs, 19)[0] or struct.unpack_from("<I", bs, 32)[0]
        data_clusters = (total * self.bps - self.datastart) // self.csize
        self.nclusters = min(data_clusters + 2, (self.fatsz * self.bps) // 4)
        self.hint = 2

    # ---- low level ----

    def _write(self, off, data):
        self.f.seek(self.base + off)
        self.f.write(data)

    def _set(self, c, value):
        old = struct.unpack_from("<I", self.fat, c * 4)[0]
        struct.pack_into("<I", self.fat, c * 4, (old & 0xF0000000) | (value & 0x0FFFFFFF))

    def _cluster_off(self, c):
        return self.datastart + (c - 2) * self.csize

    def _alloc(self, count):
        """`count` free clusters (zeroed on disk), chained; returns the first."""
        got = []
        c = max(self.hint, 2)
        limit = self.nclusters
        scanned = 0
        while len(got) < count:
            if c >= limit:
                c = 2
            if self._next(c) == 0 and c not in got:
                got.append(c)
            c += 1
            scanned += 1
            if scanned > limit:
                raise IOError("the image is full")
        for i, cl in enumerate(got):
            self._set(cl, got[i + 1] if i + 1 < len(got) else EOC)
            self._write(self._cluster_off(cl), bytes(self.csize))
        self.hint = got[-1] + 1
        return got[0]

    def _free_chain(self, c):
        for cl in list(self._chain(c)):
            self._set(cl, 0)

    def _write_chain(self, first, data):
        pos = 0
        for cl in self._chain(first):
            chunk = data[pos:pos + self.csize]
            self._write(self._cluster_off(cl), chunk + bytes(self.csize - len(chunk)))
            pos += self.csize

    def _dir_clusters(self, clus):
        return list(self._chain(clus if clus else self.rootclus))

    def _dir_entries(self, clus):
        """(byte offset on disk, 32 bytes) for every slot of the directory."""
        out = []
        for cl in self._dir_clusters(clus):
            base = self._cluster_off(cl)
            raw = self._read(base, self.csize)
            for i in range(0, self.csize, 32):
                out.append((base + i, raw[i:i + 32]))
        return out

    def flush(self):
        for k in range(self.nfats):
            self._write(self.fatstart + k * self.fatsz * self.bps, bytes(self.fat))
        if 0 < self.fsinfo < 0xFFFF:
            free = sum(1 for c in range(2, self.nclusters) if self._next(c) == 0)
            sec = bytearray(self._read(self.fsinfo * self.bps, 512))
            if sec[0:4] == b"RRaA":
                struct.pack_into("<II", sec, 488, free, self.hint)
                self._write(self.fsinfo * self.bps, bytes(sec))
        self.f.flush()

    def close(self):
        self.flush()
        self.f.close()

    # ---- directory entries ----

    def _locate(self, clus, name):
        """Slots (offsets) of `name`'s LFN entries and its short entry, and the entry bytes."""
        pending = []
        lfn = []
        for off, e in self._dir_entries(clus):
            if e[0] == 0:
                break
            if e[0] == 0xE5:
                pending, lfn = [], []
                continue
            if e[11] == ATTR_LFN:
                pending.append(off)
                lfn.insert(0, e[1:11] + e[14:26] + e[28:32])
                continue
            if lfn:
                n = b"".join(lfn).decode("utf-16-le", "replace").split("\x00")[0]
            else:
                b = e[0:8].decode("latin-1").rstrip()
                x = e[8:11].decode("latin-1").rstrip()
                n = b + ("." + x if x else "")
            if n.lower() == name.lower():
                return pending + [off], e
            pending, lfn = [], []
        return None, None

    def _short_names(self, clus):
        names = set()
        for _, e in self._dir_entries(clus):
            if e[0] == 0:
                break
            if e[0] != 0xE5 and e[11] != ATTR_LFN:
                names.add(bytes(e[0:11]))
        return names

    def _make_short(self, clus, name):
        base, _, ext = name.upper().rpartition(".")
        if not base:
            base, ext = ext, ""
        keep = lambda s: "".join(ch for ch in s if ch.isalnum() or ch in "$%'-_@~`!(){}^#&")
        base, ext = keep(base), keep(ext)[:3]
        taken = self._short_names(clus)
        for i in range(1, 100):
            tail = f"~{i}"
            short = (base[:8 - len(tail)] + tail).ljust(8).encode() + ext.ljust(3).encode()
            if short not in taken:
                return short
        raise IOError("no free short name")

    def _free_run(self, clus, count):
        """Offsets of `count` consecutive free slots, growing the directory if needed."""
        while True:
            run = []
            for off, e in self._dir_entries(clus):
                if e[0] in (0, 0xE5):
                    if run and off != run[-1] + 32:
                        run = []
                    run.append(off)
                    if len(run) == count:
                        return run
                else:
                    run = []
            last = self._dir_clusters(clus)[-1]
            new = self._alloc(1)
            self._set(last, new)

    def _add_entry(self, clus, name, attr, first, size):
        short = self._make_short(clus, name)
        units = name.encode("utf-16-le")
        chars = [units[i:i + 2] for i in range(0, len(units), 2)] + [b"\x00\x00"]
        while len(chars) % 13:
            chars.append(b"\xff\xff")
        parts = [chars[i:i + 13] for i in range(0, len(chars), 13)]
        slots = self._free_run(clus, len(parts) + 1)
        csum = _lfn_checksum(short)
        for k, part in enumerate(reversed(parts)):
            seq = len(parts) - k
            e = bytearray(32)
            e[0] = seq | (0x40 if k == 0 else 0)
            e[1:11] = b"".join(part[0:5])
            e[11] = ATTR_LFN
            e[13] = csum
            e[14:26] = b"".join(part[5:11])
            e[28:32] = b"".join(part[11:13])
            self._write(slots[k], bytes(e))
        e = bytearray(32)
        e[0:11] = short
        e[11] = attr
        struct.pack_into("<HHI", e, 20, first >> 16, 0, 0)
        struct.pack_into("<H", e, 26, first & 0xFFFF)
        struct.pack_into("<I", e, 28, size)
        struct.pack_into("<HH", e, 14, 0, 0x5A21)  # time, date (2025-01-01)
        struct.pack_into("<HH", e, 22, 0, 0x5A21)
        self._write(slots[-1], bytes(e))

    # ---- paths ----

    def _dir_of(self, path, create):
        clus = 0
        for part in [p for p in path.replace("\\", "/").split("/") if p]:
            for name, d, c, _ in self.listdir(clus):
                if name.lower() == part.lower() and d:
                    clus = c
                    break
            else:
                if not create:
                    raise FileNotFoundError(path)
                clus = self._mkdir_in(clus, part)
        return clus

    def _mkdir_in(self, parent, name):
        c = self._alloc(1)
        dot = bytearray(32)
        dot[0:11] = b".          "
        dot[11] = ATTR_DIR
        struct.pack_into("<H", dot, 20, c >> 16)
        struct.pack_into("<H", dot, 26, c & 0xFFFF)
        dotdot = bytearray(32)
        dotdot[0:11] = b"..         "
        dotdot[11] = ATTR_DIR
        p = 0 if parent in (0, self.rootclus) else parent
        struct.pack_into("<H", dotdot, 20, p >> 16)
        struct.pack_into("<H", dotdot, 26, p & 0xFFFF)
        self._write(self._cluster_off(c), bytes(dot) + bytes(dotdot))
        self._add_entry(parent, name, ATTR_DIR, c, 0)
        return c

    def put(self, path, data):
        """Write `data` as file `path`, replacing it if it exists; parents are created."""
        folder, _, name = path.replace("\\", "/").rpartition("/")
        clus = self._dir_of(folder, True)
        slots, entry = self._locate(clus, name)
        first = self._alloc((len(data) + self.csize - 1) // self.csize) if data else 0
        if data:
            self._write_chain(first, data)
        if slots:
            old = (struct.unpack_from("<H", entry, 20)[0] << 16) | struct.unpack_from("<H", entry, 26)[0]
            if old:
                self._free_chain(old)
            e = bytearray(entry)
            struct.pack_into("<H", e, 20, first >> 16)
            struct.pack_into("<H", e, 26, first & 0xFFFF)
            struct.pack_into("<I", e, 28, len(data))
            self._write(slots[-1], bytes(e))
        else:
            self._add_entry(clus, name, ATTR_ARCHIVE, first, len(data))

    def remove(self, path):
        """Delete file `path` (no error if it is not there). Returns True if it was."""
        folder, _, name = path.replace("\\", "/").rpartition("/")
        try:
            clus = self._dir_of(folder, False)
        except FileNotFoundError:
            return False
        slots, entry = self._locate(clus, name)
        if not slots:
            return False
        first = (struct.unpack_from("<H", entry, 20)[0] << 16) | struct.unpack_from("<H", entry, 26)[0]
        if first and not entry[11] & ATTR_DIR:
            self._free_chain(first)
        for off in slots:
            self._write(off, b"\xe5")
        return True

    def clear_folder(self, path):
        """Delete every file in folder `path` (kept, if it exists)."""
        try:
            clus = self._dir_of(path, False)
        except FileNotFoundError:
            return 0
        names = [n for n, d, _, _ in self.listdir(clus) if not d]
        for n in names:
            self.remove(path.rstrip("/") + "/" + n)
        return len(names)


def read_file(image, path):
    """Bytes of `path` in `image`, or None if it is not there (read only)."""
    fat = sd_extract.Fat(image)
    try:
        return fat.get(path)
    except (FileNotFoundError, IsADirectoryError):
        return None
    finally:
        fat.close()


def list_folder(image, path):
    """{name: size} of the files in folder `path` of `image` ({} if missing)."""
    fat = sd_extract.Fat(image)
    try:
        clus, isdir, _ = fat.find(path)
        if not isdir:
            return {}
        return {n: s for n, d, _, s in fat.listdir(clus) if not d}
    except FileNotFoundError:
        return {}
    finally:
        fat.close()
