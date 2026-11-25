"""Read-only FAT16/FAT32 SD card image reader and extractor.

The image is only ever opened for reading.

usage: sd-extract.py IMAGE ls PATH [DEPTH]       list a directory
       sd-extract.py IMAGE get PATH OUTFILE      copy one file out
       sd-extract.py IMAGE extract OUTDIR        copy the whole card into a new folder

`extract` refuses to write into an existing, non-empty OUTDIR. Paths inside
the image use forward slashes and are matched case-insensitively, as on FAT.
"""
import os
import struct
import sys


class Fat:
    def __init__(self, path):
        self.f = open(path, "rb")
        self.base = 0
        bs = self._read(0, 512)
        if bs[0x52:0x57] != b"FAT32" and bs[0x36:0x39] != b"FAT":
            # Partitioned image: use the first MBR partition.
            lba = struct.unpack_from("<I", bs, 0x1BE + 8)[0]
            self.base = lba * 512
            bs = self._read(0, 512)
        self.bps, self.spc, self.rsvd, self.nfats, self.rootent = struct.unpack_from("<HBHBH", bs, 11)
        fatsz16 = struct.unpack_from("<H", bs, 22)[0]
        fatsz32, = struct.unpack_from("<I", bs, 36)
        self.fat32 = fatsz16 == 0
        self.fatsz = fatsz32 if self.fat32 else fatsz16
        self.rootclus = struct.unpack_from("<I", bs, 44)[0] if self.fat32 else 0
        self.rootsecs = (self.rootent * 32 + self.bps - 1) // self.bps
        self.fatstart = self.rsvd * self.bps
        self.rootstart = (self.rsvd + self.nfats * self.fatsz) * self.bps
        self.datastart = self.rootstart + self.rootsecs * self.bps
        self.csize = self.spc * self.bps
        self.fat = self._read(self.fatstart, self.fatsz * self.bps)

    def close(self):
        self.f.close()

    def _read(self, off, n):
        self.f.seek(self.base + off)
        return self.f.read(n)

    def _next(self, c):
        if self.fat32:
            return struct.unpack_from("<I", self.fat, c * 4)[0] & 0x0FFFFFFF
        return struct.unpack_from("<H", self.fat, c * 2)[0]

    def _chain(self, c):
        end = 0x0FFFFFF8 if self.fat32 else 0xFFF8
        seen = 0
        while 2 <= c < end and seen < 10_000_000:
            yield c
            c = self._next(c)
            seen += 1

    def _runs(self, c):
        """Cluster chain as (first_cluster, count) runs of consecutive clusters."""
        start = prev = None
        for cl in self._chain(c):
            if start is None:
                start = prev = cl
            elif cl == prev + 1:
                prev = cl
            else:
                yield start, prev - start + 1
                start = prev = cl
        if start is not None:
            yield start, prev - start + 1

    def _clusters(self, c, size=None):
        out = bytearray()
        for chunk in self._stream(c, size):
            out += chunk
        return bytes(out)

    def _stream(self, c, size=None):
        """Yield the data of a cluster chain, cut to `size` bytes when given."""
        left = size
        for first, count in self._runs(c):
            n = count * self.csize
            if left is not None:
                n = min(n, left)
            yield self._read(self.datastart + (first - 2) * self.csize, n)
            if left is not None:
                left -= n
                if left <= 0:
                    return

    def _dirbytes(self, clus):
        if clus == 0 and not self.fat32:
            return self._read(self.rootstart, self.rootsecs * self.bps)
        return self._clusters(clus if clus else self.rootclus)

    def listdir(self, clus):
        data = self._dirbytes(clus)
        lfn = []
        for i in range(0, len(data), 32):
            e = data[i:i + 32]
            if e[0] == 0:
                break
            if e[0] == 0xE5:
                lfn = []
                continue
            attr = e[11]
            if attr == 0x0F:
                lfn.insert(0, e[1:11] + e[14:26] + e[28:32])
                continue
            if attr & 0x08:
                lfn = []
                continue
            if lfn:
                name = b"".join(lfn).decode("utf-16-le", "replace").split("\x00")[0]
            else:
                base = e[0:8].decode("latin-1").rstrip()
                ext = e[8:11].decode("latin-1").rstrip()
                name = base + ("." + ext if ext else "")
            lfn = []
            if name in (".", ".."):
                continue
            hi, = struct.unpack_from("<H", e, 20)
            lo, = struct.unpack_from("<H", e, 26)
            size, = struct.unpack_from("<I", e, 28)
            yield name, bool(attr & 0x10), (hi << 16) | lo, size

    def find(self, path):
        clus, isdir, size = 0, True, 0
        for part in [p for p in path.replace("\\", "/").split("/") if p]:
            for name, d, c, s in self.listdir(clus):
                if name.lower() == part.lower():
                    clus, isdir, size = c, d, s
                    break
            else:
                raise FileNotFoundError(path)
        return clus, isdir, size

    def walk(self, clus, depth, prefix=""):
        for name, d, c, s in sorted(self.listdir(clus), key=lambda x: (not x[1], x[0].lower())):
            if d:
                print(f"{prefix}{name}/")
                if depth > 1:
                    self.walk(c, depth - 1, prefix + "  ")
            else:
                print(f"{prefix}{name}  {s}")

    def get(self, path):
        clus, isdir, size = self.find(path)
        if isdir:
            raise IsADirectoryError(path)
        return self._clusters(clus, size) if size else b""

    def extract(self, clus, outdir, stats):
        """Copy directory `clus` (0: root) and everything below it into `outdir`."""
        os.makedirs(outdir, exist_ok=True)
        for name, d, c, s in self.listdir(clus):
            if name in ("", ".", "..") or "/" in name or "\\" in name or ":" in name:
                raise ValueError(f"refusing unsafe name {name!r} in {outdir}")
            target = os.path.join(outdir, name)
            if d:
                stats[0] += 1
                self.extract(c, target, stats)
            else:
                with open(target, "xb") as out:
                    written = 0
                    if s:
                        for chunk in self._stream(c, s):
                            out.write(chunk)
                            written += len(chunk)
                if written != s:
                    raise IOError(f"{target}: read {written} of {s} bytes (truncated chain)")
                stats[1] += 1
                stats[2] += s


def main():
    if len(sys.argv) < 4:
        raise SystemExit(__doc__)
    img, cmd = sys.argv[1], sys.argv[2]
    fat = Fat(img)
    try:
        if cmd == "ls":
            depth = int(sys.argv[4]) if len(sys.argv) > 4 else 1
            clus, isdir, _ = fat.find(sys.argv[3])
            if not isdir:
                raise NotADirectoryError(sys.argv[3])
            fat.walk(clus, depth)
        elif cmd == "get" and len(sys.argv) > 4:
            with open(sys.argv[4], "wb") as out:
                out.write(fat.get(sys.argv[3]))
        elif cmd == "extract":
            outdir = sys.argv[3]
            if os.path.isdir(outdir) and os.listdir(outdir):
                raise SystemExit(f"error: {outdir} exists and is not empty")
            stats = [0, 0, 0]
            fat.extract(0, outdir, stats)
            print(f"extracted {stats[1]} files in {stats[0]} folders, {stats[2]} bytes, to {outdir}")
        else:
            raise SystemExit(__doc__)
    finally:
        fat.close()


if __name__ == "__main__":
    main()
