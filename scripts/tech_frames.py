"""Prints Project+'s table of first distinguishable tech frames
(src/main/game/tech_frames.cpp, kDistinguishable) from Project+'s fighter
files.

usage: tech_frames.py FIGHTER_DIR [--threshold PERCENT] [--series FILE]

FIGHTER_DIR is Project+'s pf/fighter folder (on the SD card). For each
fighter the script reads the animation file (Fit<Name>Motion.pac or
Fit<Name>MotionEtc.pac: the CHR0 animations Passive, PassiveStandF and
PassiveStandB) and the skeleton of its first costume file
(Fit<Name>NN.pac, LZ11 compressed: the MDL0's bones). It poses the
skeleton on every frame of the three animations and measures how far apart
two animations are: the mean distance between the same bones, as a
percentage of the fighter's height (the bind pose's vertical extent; the
model-part and face bones, names ending in M, left out). The first
distinguishable frame is the first frame (1: the tech's first) on which all
three pairs are at least the threshold apart (16%: set by frame-stepped
screenshots of Mario, Fox, Captain Falcon and Bowser in the emulator, see
docs/testing.md, test-drill-events --only techchase-frames).

Nothing of the files is written out; the output is one C row per fighter
kind. Standard library only.
"""
import json
import math
import os
import re
import struct
import sys

sys.dont_write_bytecode = True


def u8(d, o): return d[o]
def u16(d, o): return struct.unpack_from(">H", d, o)[0]
def s16(d, o): return struct.unpack_from(">h", d, o)[0]
def u32(d, o): return struct.unpack_from(">I", d, o)[0]
def s32(d, o): return struct.unpack_from(">i", d, o)[0]
def f32(d, o): return struct.unpack_from(">f", d, o)[0]


def lz11(d):
    if d[0] != 0x11:
        return d
    size = d[1] | d[2] << 8 | d[3] << 16
    p = 4
    if size == 0:
        size = struct.unpack_from("<I", d, 4)[0]
        p = 8
    out = bytearray()
    while len(out) < size:
        flags = d[p]; p += 1
        for bit in range(8):
            if len(out) >= size:
                break
            if flags & (0x80 >> bit):
                b0 = d[p]
                ind = b0 >> 4
                if ind == 0:
                    b1, b2 = d[p + 1], d[p + 2]
                    n = ((b0 & 0xF) << 4 | b1 >> 4) + 0x11
                    disp = ((b1 & 0xF) << 8 | b2) + 1
                    p += 3
                elif ind == 1:
                    b1, b2, b3 = d[p + 1], d[p + 2], d[p + 3]
                    n = ((b0 & 0xF) << 12 | b1 << 4 | b2 >> 4) + 0x111
                    disp = ((b2 & 0xF) << 8 | b3) + 1
                    p += 4
                else:
                    b1 = d[p + 1]
                    n = ind + 1
                    disp = ((b0 & 0xF) << 8 | b1) + 1
                    p += 2
                for _ in range(n):
                    out.append(out[-disp])
            else:
                out.append(d[p]); p += 1
    return bytes(out)


def name_at(d, o):
    e = d.index(b"\0", o)
    return d[o:e].decode("ascii", "replace")


def resdic(d, base):
    """{name: absolute data offset} of a ResDic at `base`."""
    count = u32(d, base + 4)
    out = {}
    for i in range(1, count + 1):
        e = base + 8 + 16 * i
        no, do = s32(d, e + 8), s32(d, e + 12)
        out[name_at(d, base + no)] = base + do
    return out


def brres_list(d):
    """Every BRRES in a file: offsets."""
    out = []
    i = d.find(b"bres")
    while i >= 0:
        if u16(d, i + 4) == 0xFEFF:
            out.append(i)
        i = d.find(b"bres", i + 4)
    return out


def brres_folders(d, b):
    root = b + u16(d, b + 0xC)
    folders = resdic(d, root + 8)
    return {k: resdic(d, v) for k, v in folders.items()}


# ---- CHR0 ----

def _hermite(f, k0, k1):
    f0, v0, t0 = k0
    f1, v1, t1 = k1
    span = f1 - f0
    if span <= 0:
        return v0
    t = (f - f0) / span
    inv = t - 1.0
    return v0 + (f - f0) * inv * (inv * t0 + t * t1) + t * t * (3.0 - 2.0 * t) * (v1 - v0)


class Track:
    def __init__(self, keys=None, values=None, const=None):
        self.keys, self.values, self.const = keys, values, const

    def at(self, f):
        if self.const is not None:
            return self.const
        if self.values is not None:
            i = int(math.floor(f))
            i = max(0, min(i, len(self.values) - 1))
            j = min(i + 1, len(self.values) - 1)
            a = f - i
            return self.values[i] * (1 - a) + self.values[j] * a
        ks = self.keys
        if f <= ks[0][0]:
            return ks[0][1]
        if f >= ks[-1][0]:
            return ks[-1][1]
        for i in range(len(ks) - 1):
            if ks[i][0] <= f <= ks[i + 1][0]:
                return _hermite(f, ks[i], ks[i + 1])
        return ks[-1][1]


def _read_track(d, o, fmt, frames, rot):
    if fmt == 1:  # I4
        n = u16(d, o); step, base = f32(d, o + 8), f32(d, o + 12)
        keys = []
        for i in range(n):
            w = u32(d, o + 16 + 4 * i)
            fr = w >> 24
            v = (w >> 12) & 0xFFF
            t = w & 0xFFF
            if t & 0x800:
                t -= 0x1000
            keys.append((float(fr), base + step * v, t / 32.0))
        return Track(keys=keys)
    if fmt == 2:  # I6
        n = u16(d, o); step, base = f32(d, o + 8), f32(d, o + 12)
        keys = []
        for i in range(n):
            q = o + 16 + 6 * i
            keys.append((u16(d, q) / 32.0, base + step * u16(d, q + 2), s16(d, q + 4) / 256.0))
        return Track(keys=keys)
    if fmt == 3:  # I12
        n = u16(d, o)
        keys = [(f32(d, o + 8 + 12 * i), f32(d, o + 12 + 12 * i), f32(d, o + 16 + 12 * i)) for i in range(n)]
        return Track(keys=keys)
    if rot and fmt == 4:  # L1
        step, base = f32(d, o), f32(d, o + 4)
        return Track(values=[base + step * d[o + 8 + i] for i in range(frames + 1)])
    if rot and fmt == 5:  # L2
        step, base = f32(d, o), f32(d, o + 4)
        return Track(values=[base + step * u16(d, o + 8 + 2 * i) for i in range(frames + 1)])
    if rot and fmt == 6:  # L4
        return Track(values=[f32(d, o + 4 * i) for i in range(frames + 1)])
    raise ValueError(f"track format {fmt}")


class Chr0:
    def __init__(self, d, o):
        if d[o:o + 4] != b"CHR0":
            raise ValueError("not CHR0")
        ver = u32(d, o + 8)
        if ver == 4:
            data, frames = s32(d, o + 0x10), u16(d, o + 0x1C)
        elif ver == 5:
            data, frames = s32(d, o + 0x10), u16(d, o + 0x20)
        else:
            raise ValueError(f"CHR0 v{ver}")
        self.frames = frames
        self.bones = {}
        self.formats = set()
        for name, e in resdic(d, o + data).items():
            self.bones[name] = self._entry(d, e, frames)

    def _entry(self, d, e, frames):
        code = u32(d, e + 4)
        p = e + 8
        out = {}
        groups = (("s", 22, 4, 13, 25, 0x3), ("r", 23, 5, 16, 27, 0x7), ("t", 24, 6, 19, 30, 0x3))
        for key, has, iso, fixed0, fshift, fmask in groups:
            if not code & (1 << has):
                continue
            fmt = (code >> fshift) & fmask
            self.formats.add((key, fmt))
            comps = 1 if code & (1 << iso) else 3
            tr = []
            for c in range(comps):
                if code & (1 << (fixed0 + c)) if comps == 3 else code & (1 << fixed0):
                    tr.append(Track(const=f32(d, p)))
                else:
                    tr.append(_read_track(d, e + s32(d, p), fmt, frames, key == "r"))
                p += 4
            out[key] = tr * 3 if comps == 1 else tr
        return out


def find_chr0(d, names):
    """{name: Chr0} for the animations in `names`, from every BRRES in d."""
    out = {}
    for b in brres_list(d):
        try:
            folders = brres_folders(d, b)
        except Exception:
            continue
        for fname, entries in folders.items():
            if not fname.startswith("AnmChr"):
                continue
            for n, off in entries.items():
                if n in names and n not in out:
                    out[n] = Chr0(d, off)
    return out


# ---- MDL0 bones ----

class Bone:
    pass


def mdl0_bones(d):
    """The first model's bones: {name: Bone(parent, s, r, t)}."""
    for b in brres_list(d):
        try:
            folders = brres_folders(d, b)
        except Exception:
            continue
        for fname, entries in folders.items():
            if not fname.startswith("3DModels"):
                continue
            for _, m in entries.items():
                return _bones(d, m)
    return None


def _bones(d, m):
    ver = u32(d, m + 8)
    # The section offsets follow the header (0x10); Bones is the second list.
    nsec = {8: 11, 9: 11, 10: 14, 11: 14}[ver]
    secs = [s32(d, m + 0x10 + 4 * i) for i in range(nsec)]
    bones_dic = m + secs[1]
    out = {}
    by_off = {}
    for name, off in resdic(d, bones_dic).items():
        bo = Bone()
        bo.name = name
        bo.s = struct.unpack_from(">3f", d, off + 0x20)
        bo.r = struct.unpack_from(">3f", d, off + 0x2C)
        bo.t = struct.unpack_from(">3f", d, off + 0x38)
        po = s32(d, off + 0x5C)
        bo.parent_off = off + po if po else None
        out[name] = bo
        by_off[off] = bo
    for bo in out.values():
        p = by_off.get(bo.parent_off)
        bo.parent = p.name if p else None
    return out


# ---- posing ----

def euler(rx, ry, rz):
    """NW4R's rotation: X first, then Y, then Z (M = Rz Ry Rx), degrees."""
    x, y, z = (math.radians(a) for a in (rx, ry, rz))
    cx, sx, cy, sy, cz, sz = math.cos(x), math.sin(x), math.cos(y), math.sin(y), math.cos(z), math.sin(z)
    return [[cy * cz, sx * sy * cz - cx * sz, cx * sy * cz + sx * sz],
            [cy * sz, sx * sy * sz + cx * cz, cx * sy * sz - sx * cz],
            [-sy, sx * cy, cx * cy]]


def matmul(a, b):
    return [[sum(a[i][k] * b[k][j] for k in range(3)) for j in range(3)] for i in range(3)]


def pose(bones, anim, f):
    """World positions of every bone at animation frame f (0-based)."""
    world = {}

    def get(name):
        if name in world:
            return world[name]
        bo = bones[name]
        a = anim.bones.get(name, {}) if anim else {}
        s = [t.at(f) for t in a["s"]] if "s" in a else list(bo.s)
        r = [t.at(f) for t in a["r"]] if "r" in a else list(bo.r)
        t = [tr.at(f) for tr in a["t"]] if "t" in a else list(bo.t)
        R = euler(*r)
        RS = [[R[i][j] * s[j] for j in range(3)] for i in range(3)]
        if bo.parent is None:
            res = (RS, t)
        else:
            pm, pt = get(bo.parent)
            m = matmul(pm, RS)
            res = (m, [pt[i] + sum(pm[i][k] * t[k] for k in range(3)) for i in range(3)])
        world[name] = res
        return res
    for n in bones:
        get(n)
    return {n: v[1] for n, v in world.items()}


# ---- the table ----

ANIMS = ("Passive", "PassiveStandF", "PassiveStandB")
# ftKind -> (folder in pf/fighter, name): Project+ 3.2.1's playable fighters.
KINDS = {0x00: ("MARIO", "Mario"), 0x01: ("DONKEY", "Donkey Kong"), 0x02: ("LINK", "Link"),
         0x03: ("SAMUS", "Samus"), 0x04: ("YOSHI", "Yoshi"), 0x05: ("KIRBY", "Kirby"), 0x06: ("FOX", "Fox"),
         0x07: ("PIKACHU", "Pikachu"), 0x08: ("LUIGI", "Luigi"), 0x09: ("CAPTAIN", "Captain Falcon"),
         0x0A: ("NESS", "Ness"), 0x0B: ("KOOPA", "Bowser"), 0x0C: ("PEACH", "Peach"), 0x0D: ("ZELDA", "Zelda"),
         0x0E: ("SHEIK", "Sheik"), 0x0F: ("POPO", "Popo"), 0x10: ("POPO", "Nana"), 0x11: ("MARTH", "Marth"),
         0x12: ("GAMEWATCH", "Mr. Game & Watch"), 0x13: ("FALCO", "Falco"), 0x14: ("GANON", "Ganondorf"),
         0x15: ("WARIO", "Wario"), 0x16: ("METAKNIGHT", "Meta Knight"), 0x17: ("PIT", "Pit"),
         0x18: ("SZEROSUIT", "Zero Suit Samus"), 0x19: ("PIKMIN", "Olimar"), 0x1A: ("LUCAS", "Lucas"),
         0x1B: ("DIDDY", "Diddy Kong"), 0x1D: ("POKELIZARDON", "Charizard"), 0x1E: ("POKEZENIGAME", "Squirtle"),
         0x1F: ("POKEFUSHIGISOU", "Ivysaur"), 0x20: ("DEDEDE", "King Dedede"), 0x21: ("LUCARIO", "Lucario"),
         0x22: ("IKE", "Ike"), 0x23: ("ROBOT", "R.O.B."), 0x25: ("PURIN", "Jigglypuff"), 0x26: ("MEWTWO", "Mewtwo"),
         0x27: ("ROY", "Roy"), 0x29: ("TOONLINK", "Toon Link"), 0x2C: ("WOLF", "Wolf"),
         0x2D: ("KNUCKLES", "Knuckles"), 0x2E: ("SNAKE", "Snake"), 0x2F: ("SONIC", "Sonic"),
         0x30: ("GKOOPA", "Giga Bowser")}
OTHER = {0x1C: "Pokemon Trainer", 0x24: "(none)", 0x28: "Dr. Mario", 0x2A: "(none)", 0x2B: "(none)",
         0x31: "Wario-Man"}
TABLE_SIZE = 0x32


def folder_of(root, name):
    for entry in os.listdir(root):
        if entry.lower() == name.lower():
            return os.path.join(root, entry)
    return None


def series(root, folder, frames=25):
    """Per frame: the three pairs' distances (in place-forward, in place-back,
    forward-back), % of the height."""
    path = folder_of(root, folder)
    if path is None:
        return None, "no folder"
    files = os.listdir(path)
    motion = [f for f in files if re.match(r"Fit\w+Motion(Etc)?\.pac$", f, re.I)]
    models = sorted(f for f in files if re.match(r"Fit[A-Za-z]+\d\d\.pac$", f))
    if not motion or not models:
        return None, "no animation or costume file"
    with open(os.path.join(path, motion[0]), "rb") as f:
        anims = find_chr0(f.read(), set(ANIMS))
    if len(anims) < 3:
        return None, f"animations found: {sorted(anims)}"
    with open(os.path.join(path, models[0]), "rb") as f:
        bones = mdl0_bones(lz11(f.read()))

    def skeleton(n):
        b = bones[n]
        while b is not None:
            if b.name.endswith("M") or b.name == "FacePattern":
                return False
            b = bones.get(b.parent) if b.parent else None
        return True
    keep = [n for n in bones if skeleton(n)]
    bind = pose(bones, None, 0)
    ys = [bind[n][1] for n in keep]
    height = max(ys) - min(ys)
    out = []
    for fr in range(frames):
        p = [pose(bones, anims[n], fr) for n in ANIMS]

        def apart(a, b):
            return sum(math.dist(a[n], b[n]) for n in keep) / len(keep) / height * 100
        out.append((apart(p[0], p[1]), apart(p[0], p[2]), apart(p[1], p[2])))
    return out, f"{motion[0]}, {models[0]}, frames {[anims[n].frames for n in ANIMS]}"


def main(argv):
    if not argv or argv[0].startswith("-"):
        print(__doc__)
        return 2
    root = argv[0]
    threshold = float(argv[argv.index("--threshold") + 1]) if "--threshold" in argv else 16.0
    dump = argv[argv.index("--series") + 1] if "--series" in argv else None
    rows, kept = [], {}
    for kind in range(TABLE_SIZE):
        if kind not in KINDS:
            rows.append(f"        -1, // 0x{kind:02X} {OTHER.get(kind, '(none)')}: not a fighter of its own here")
            continue
        folder, name = KINDS[kind]
        s, why = series(root, folder)
        if s is None:
            rows.append(f"        -1, // 0x{kind:02X} {name}: missing ({why})")
            continue
        first = next((i + 1 for i, x in enumerate(s) if min(x) >= threshold), -1)
        kept[kind] = {"name": name, "files": why, "series": s}
        rows.append(f"        {first:>2}, // 0x{kind:02X} {name}")
        print(f"{name}: {first} ({why})", file=sys.stderr)
    print("\n".join(rows))
    if dump:
        with open(dump, "w") as f:
            json.dump(kept, f, indent=0)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
