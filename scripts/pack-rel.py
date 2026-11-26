"""Pack the stub REL and the modules it loads into the one plugin file.

usage: pack-rel.py pack --stub STUB.rel --module KIND:ID:FILE.rel [--module ...] -o OUT.rel
                        [--stub-budget BYTES] [--temp-budget BYTES] [--budget KIND=BYTES ...]
                        [--far-calls KIND ...]
                        [--break badversion|corrupt|truncated] [--break-module KIND] [--pad BYTES]
       pack-rel.py info FILE.rel

KIND is a module kind the stub knows (match, menu, extra); ID its REL module
id. A module may import from another module in the same file (the extra
module links against the match module); the size lines name those imports.

Layout (src/stub/packed_file.h reads it; keep the two in step):

    [stub REL][zeros to 32][module REL, zero-padded to 32]...[table][trailer]

All fields are big-endian u32. The table has one 32-byte entry per module:
id, kind tag ("MTCH", "MENU", "XTRA"), offset, padded size, FNV-1a (32-bit) of
those bytes, three zero words. The trailer is 32 bytes: magic "TMPK",
format version 2, number of entries, offset of the table, four zero words.

Syriinge links only the stub: gfModule::create copies the REL's first
fixSize bytes (header field 0x48) and never looks past them.

The stub links each module as the game links its own (gfModule::create,
src/stub/loader.cpp): it reads the module's REL into a temporary buffer,
copies the first fixSize bytes into a permanent block of fixSize + .bss
(each rounded up to 32), links with OSLinkFixed and .bss at fixSize,
repoints the kept import table into the permanent block, and frees the
temporary buffer. The permanent block is the module's "bytes loaded" and
what its budget limits; the largest module file is reported as the load
temporary buffer requirement. OSLinkFixed keeps only the import entries
before the first one from the game (module 0) or the module itself, so the
build fails unless those come last and everything kept lies before fixSize
(check_fixed).

--far-calls KIND appends a 16-byte PowerPC far-call trampoline island to KIND's
.text section (lis r12, target@h; ori r12, r12, target@l; mtctr r12; bctr) per
unique external R_PPC_REL24 target, rewrites each external bl/b site to branch
locally to its stub (preserving LK bit 0), and replaces the external
R_PPC_REL24 records with R_PPC_ADDR16_HI + R_PPC_ADDR16_LO records so a module
loaded in MEM2 (AssistFigureResource) can call MEM1 targets (>32 MiB away).

--break writes a deliberately broken file for the loader's negative tests,
to the first module, or to the module of kind KIND with --break-module:
  badversion  the module reports interface version + 1
  corrupt     one byte of the module's code flipped after checksumming
  truncated   the file cut short, losing the trailer
--pad puts BYTES of zeros between the stub and the first module, for the
loader's test of a file far larger than the heaps it could once be read into.
"""
import argparse
import os
import re
import struct
import sys

sys.dont_write_bytecode = True

TRAILER_MAGIC = 0x544D504B  # "TMPK"
FORMAT_VERSION = 2
TRAILER_SIZE = 0x20
ENTRY = struct.Struct(">8I")
KINDS = {"match": 0x4D544348, "menu": 0x4D454E55, "extra": 0x58545241}
KIND_NAMES = {v: k for k, v in KINDS.items()}
MODULE_MAGIC = b"TMMD"
REL_HEADER = struct.Struct(">12I4B6I")  # OSModuleHeader, 0x4C bytes

R_PPC_ADDR16_LO = 4
R_PPC_ADDR16_HI = 5
R_PPC_REL24 = 10
R_DOLPHIN_NOP = 201
R_DOLPHIN_SECTION = 202
R_DOLPHIN_END = 203

# 16-byte MEM2 -> MEM1 far-call trampoline stub (r12 and CTR are volatile in
# PPC EABI and untouched by _savegpr/_restgpr/_savefpr/_restfpr, which use r11):
#   lis   r12, target@h       ; 0x3D800000 (R_PPC_ADDR16_HI at +2)
#   ori   r12, r12, target@l  ; 0x618C0000 (R_PPC_ADDR16_LO at +6)
#   mtctr r12                 ; 0x7D8903A6
#   bctr                      ; 0x4E800420
STUB_BYTES = 16
STUB_TEMPLATE = struct.pack(">4I", 0x3D800000, 0x618C0000, 0x7D8903A6, 0x4E800420)


def align(n, a=32):
    return (n + a - 1) & ~(a - 1)


def fnv1a(data):
    h = 0x811C9DC5
    for b in data:
        h = ((h ^ b) * 0x01000193) & 0xFFFFFFFF
    return h


def rel_header(data, what):
    if len(data) < REL_HEADER.size:
        raise SystemExit(f"error: {what} is too small for a REL header")
    h = dict(zip(("id", "next", "prev", "num_sections", "section_info", "name_offset", "name_size",
                  "version", "bss_size", "rel_offset", "imp_offset", "imp_size", "prolog_section",
                  "epilog_section", "unresolved_section", "bss_section", "prolog", "epilog",
                  "unresolved", "align", "bss_align", "fix_size"), REL_HEADER.unpack_from(data)))
    if h["version"] != 3:
        raise SystemExit(f"error: {what} is a version {h['version']} REL, expected 3")
    h["sections"] = [struct.unpack_from(">2I", data, h["section_info"] + 8 * i)
                     for i in range(h["num_sections"])]
    return h


def imports(data, h):
    """(module id, offset of its relocation list) per import entry, in order (0 is the game's main program)."""
    return [struct.unpack_from(">2I", data, h["imp_offset"] + i) for i in range(0, h["imp_size"], 8)]


def imported_ids(data, h):
    return [mid for mid, _ in imports(data, h)]


def decode_relocs(data, at, what):
    """Decode one import's relocation list into [(cur_sec, cur_off, rtype, target_sec, addend)]."""
    out = []
    cur_sec, cur_off = 0, 0
    while at + 8 <= len(data):
        delta, rtype, sec, addend = struct.unpack_from(">HBBI", data, at)
        at += 8
        if rtype == R_DOLPHIN_END:
            return out
        if rtype == R_DOLPHIN_SECTION:
            cur_sec, cur_off = sec, 0
            continue
        cur_off += delta
        if rtype == R_DOLPHIN_NOP:
            continue
        out.append((cur_sec, cur_off, rtype, sec, addend))
    raise SystemExit(f"error: {what}: a relocation list runs past the end of the file")


def encode_relocs(records):
    """Encode [(cur_sec, cur_off, rtype, target_sec, addend)] into a REL relocation list,
    inserting R_DOLPHIN_NOP (delta 0xFFFF) spacers whenever an offset delta exceeds 0xFFFF."""
    buf = bytearray()
    by_sec = {}
    for idx, (cur_sec, cur_off, rtype, target_sec, addend) in enumerate(records):
        by_sec.setdefault(cur_sec, []).append((cur_off, idx, rtype, target_sec, addend))
    for cur_sec in sorted(by_sec):
        buf += struct.pack(">HBBI", 0, R_DOLPHIN_SECTION, cur_sec, 0)
        prev_off = 0
        for cur_off, _idx, rtype, target_sec, addend in sorted(by_sec[cur_sec]):
            diff = cur_off - prev_off
            if diff < 0:
                raise SystemExit(f"error: negative relocation delta {diff} in section {cur_sec}")
            while diff > 0xFFFF:
                buf += struct.pack(">HBBI", 0xFFFF, R_DOLPHIN_NOP, 0, 0)
                diff -= 0xFFFF
            buf += struct.pack(">HBBI", diff, rtype, target_sec, addend)
            prev_off = cur_off
    buf += struct.pack(">HBBI", 0, R_DOLPHIN_END, 0, 0)
    return bytes(buf)


def count_external_rel24(data, h, what):
    """Count R_PPC_REL24 relocation entries across all external imports (mid != self)."""
    total = 0
    for mid, off in imports(data, h):
        if mid == h["id"]:
            continue
        for _cur_sec, _cur_off, rtype, _target_sec, _addend in decode_relocs(data, off, what):
            if rtype == R_PPC_REL24:
                total += 1
    return total


def synthesize_far_trampolines(data, h, what):
    """Rewrite every external R_PPC_REL24 call/tail-call in .text (section 1) to
    branch locally to an appended 16-byte far-call trampoline stub, replacing
    those R_PPC_REL24 records with R_PPC_ADDR16_HI + R_PPC_ADDR16_LO pairs.

    Returns (new_rel_bytearray, new_header, stub_count, site_count).
    """
    text_sec = 1
    if text_sec >= h["num_sections"]:
        raise SystemExit(f"error: {what}: no section 1 (.text)")
    text_raw_off, text_orig_size = h["sections"][text_sec]
    if (text_raw_off & 1) == 0 or (text_raw_off & ~1) == 0:
        raise SystemExit(f"error: {what}: section 1 is not an executable .text section")
    text_off = text_raw_off & ~1
    text_bytes = bytearray(data[text_off:text_off + text_orig_size])
    island_base = align(len(text_bytes), 4)
    if island_base > len(text_bytes):
        text_bytes += bytes(island_base - len(text_bytes))

    entries = imports(data, h)
    last = [i for i, (mid, _) in enumerate(entries) if mid in (0, h["id"])]
    first_dropped = last[0] if last else len(entries)
    for mid, _ in entries[first_dropped:]:
        if mid not in (0, h["id"]):
            raise SystemExit(f"error: {what}: kept import {mid} comes after dropped imports")

    stub_map = {}   # (mid, target_sec, addend) -> stub_off inside .text
    stub_count = 0
    site_count = 0
    new_relocs = []

    for mid, rel_off in entries:
        recs = decode_relocs(data, rel_off, what)
        if mid == h["id"]:
            new_relocs.append(recs)
            continue
        out_recs = []
        for cur_sec, cur_off, rtype, target_sec, addend in recs:
            if rtype != R_PPC_REL24:
                out_recs.append((cur_sec, cur_off, rtype, target_sec, addend))
                continue
            if cur_sec != text_sec:
                raise SystemExit(f"error: {what}: external R_PPC_REL24 in non-.text section {cur_sec} +0x{cur_off:x}")
            if (cur_off & 3) != 0 or cur_off + 4 > text_orig_size:
                raise SystemExit(f"error: {what}: R_PPC_REL24 offset 0x{cur_off:x} out of .text bounds")
            (word,) = struct.unpack_from(">I", text_bytes, cur_off)
            if (word >> 26) != 18 or (word & 2) != 0:
                raise SystemExit(f"error: {what}: unexpected instruction 0x{word:08x} at .text+0x{cur_off:x}")
            key = (mid, target_sec, addend)
            stub_off = stub_map.get(key)
            if stub_off is None:
                stub_off = island_base + stub_count * STUB_BYTES
                stub_map[key] = stub_off
                stub_count += 1
                out_recs.append((text_sec, stub_off + 2, R_PPC_ADDR16_HI, target_sec, addend))
                out_recs.append((text_sec, stub_off + 6, R_PPC_ADDR16_LO, target_sec, addend))
            delta = stub_off - cur_off
            if delta < -0x02000000 or delta >= 0x02000000 or (delta & 3) != 0:
                raise SystemExit(f"error: {what}: local trampoline branch delta 0x{delta:x} out of range")
            lk = word & 1
            patched = 0x48000000 | (delta & 0x03FFFFFC) | lk
            struct.pack_into(">I", text_bytes, cur_off, patched)
            site_count += 1
        new_relocs.append(out_recs)

    if site_count == 0:
        return data, h, 0, 0

    text_bytes += STUB_TEMPLATE * stub_count

    # Rebuild the REL cleanly: header, section table, optional name, all
    # present sections (preserving alignment <= 32), import table, kept
    # relocation lists, fixSize boundary, dropped relocation lists.
    out = bytearray(REL_HEADER.size)
    new_sec_info = len(out)
    out += bytes(8 * h["num_sections"])

    new_name_off = 0
    if h["name_size"] > 0:
        new_name_off = len(out)
        out += data[h["name_offset"]:h["name_offset"] + h["name_size"]]

    present = [(s, off & ~1, off & 1, size) for s, (off, size) in enumerate(h["sections"]) if (off & ~1) != 0]
    present.sort(key=lambda item: item[1])
    new_sections = list(h["sections"])
    for s, orig_off, exec_flag, orig_size in present:
        sec_align = min(32, orig_off & -orig_off)
        pad = align(len(out), sec_align) - len(out)
        if pad:
            out += bytes(pad)
        sec_data = bytes(text_bytes) if s == text_sec else bytes(data[orig_off:orig_off + orig_size])
        new_sections[s] = (len(out) | exec_flag, len(sec_data))
        out += sec_data

    pad = align(len(out), 4) - len(out)
    if pad:
        out += bytes(pad)
    new_imp_off = len(out)
    new_imp_size = 8 * len(entries)
    out += bytes(new_imp_size)

    imp_rel_offsets = [0] * len(entries)
    for idx in range(first_dropped):
        pad = align(len(out), 4) - len(out)
        if pad:
            out += bytes(pad)
        imp_rel_offsets[idx] = len(out)
        out += encode_relocs(new_relocs[idx])

    new_fix_size = len(out)

    for idx in range(first_dropped, len(entries)):
        pad = align(len(out), 4) - len(out)
        if pad:
            out += bytes(pad)
        imp_rel_offsets[idx] = len(out)
        out += encode_relocs(new_relocs[idx])

    new_rel_off = imp_rel_offsets[0] if imp_rel_offsets else new_fix_size

    REL_HEADER.pack_into(
        out, 0,
        h["id"], h["next"], h["prev"], h["num_sections"], new_sec_info, new_name_off, h["name_size"],
        h["version"], h["bss_size"], new_rel_off, new_imp_off, new_imp_size,
        h["prolog_section"], h["epilog_section"], h["unresolved_section"], h["bss_section"],
        h["prolog"], h["epilog"], h["unresolved"], h["align"], h["bss_align"], new_fix_size
    )
    for s, (off, size) in enumerate(new_sections):
        struct.pack_into(">2I", out, new_sec_info + 8 * s, off, size)
    for idx, (mid, _) in enumerate(entries):
        struct.pack_into(">2I", out, new_imp_off + 8 * idx, mid, imp_rel_offsets[idx])

    new_h = rel_header(out, what)
    rem = count_external_rel24(out, new_h, what)
    if rem != 0:
        raise SystemExit(f"error: {what}: {rem} external R_PPC_REL24 relocation(s) remain after trampoline synthesis")
    check_fixed(out, new_h, what)
    return out, new_h, stub_count, site_count


def check_extra_thunks(data, h, path, match_id):
    """Fail if any 4-byte tail-call slot in main_thunks.o resolved locally
    instead of relocating against the match module (which would recurse into
    an inline wrapper)."""
    map_path = path[:-4] + ".map" if path.endswith(".rel") else ""
    if not map_path or not os.path.exists(map_path):
        return
    m = re.search(r"^\s+([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+[0-9a-fA-F]+\s+[0-9a-fA-F]+\s+\d+\s+\.text\s+main_thunks\.o\b",
                  open(map_path, encoding="utf-8", errors="replace").read(), re.M)
    if not m:
        return
    base, size = int(m.group(1), 16), int(m.group(2), 16)
    if size == 0 or (size & 3) != 0:
        raise SystemExit(f"error: {path}: unexpected main_thunks.o .text size 0x{size:x}")
    covered = set()
    for mid, at in imports(data, h):
        if mid != match_id:
            continue
        for cur_sec, cur_off, _rtype, _sec, _addend in decode_relocs(data, at, path):
            if cur_sec == 1 and base <= cur_off < base + size:
                covered.add(cur_off - base)
    missing = [off for off in range(0, size, 4) if off not in covered]
    if missing:
        offs = ", ".join(f"+0x{o:x}" for o in missing[:8])
        raise SystemExit(f"error: {path}: main_thunks.o has {len(missing)}/{size // 4} slot(s) not relocated "
                         f"against match module {match_id} ({offs}); check static inline thunk wrappers")


def relocations_end(data, at, what):
    """Offset just past a relocation list (its R_DOLPHIN_END entry included)."""
    while at + 8 <= len(data):
        kind = data[at + 2]
        at += 8
        if kind == R_DOLPHIN_END:
            return at
    raise SystemExit(f"error: {what}: a relocation list runs past the end of the file")


def check_fixed(data, h, what):
    """Fails unless the REL keeps working after OSLinkFixed with .bss at fixSize: the
    imports from the game and the module itself come last (OSLinkFixed drops the
    import table from the first of them on: Brawl decompilation, OSLink.c:246-255), and the header's
    tables, the section data and the relocation lists of the imports it keeps all
    end at or before fixSize (.bss overwrites what follows)."""
    fix = h["fix_size"]
    if fix > len(data):
        raise SystemExit(f"error: {what}: fixSize 0x{fix:x} is past the end of the file")
    ends = {"the section table": h["section_info"] + 8 * h["num_sections"],
            "the import table": h["imp_offset"] + h["imp_size"]}
    for i, (offset, size) in enumerate(h["sections"]):
        if offset & ~1:
            ends[f"section {i}"] = (offset & ~1) + size
    entries = imports(data, h)
    last = [i for i, (mid, _) in enumerate(entries) if mid in (0, h["id"])]
    first_dropped = last[0] if last else len(entries)
    for mid, offset in entries[first_dropped:]:
        if mid not in (0, h["id"]):
            raise SystemExit(f"error: {what}: its imports from module {mid} come after those from the game or "
                             "itself, so OSLinkFixed would drop them")
    for mid, offset in entries[:first_dropped]:
        ends[f"the relocations against module {mid}"] = relocations_end(data, offset, what)
    for name, end in ends.items():
        if end > fix:
            raise SystemExit(f"error: {what}: {name} ends at 0x{end:x}, past fixSize 0x{fix:x}")


# Which kinds may import which: the stub loads an extra module only right
# after the match module, and unloads it first.
MAY_IMPORT = {"extra": {"match"}}


def code_offset(h):
    """Offset of a byte inside a module's first executable section."""
    for offset, size in h["sections"]:
        if offset & 1 and size > 0x40:
            return (offset & ~1) + 0x20
    raise SystemExit("error: the module has no executable section")


def skew_version(rel):
    """Raise the interface version in a module's description by one."""
    hits = [i for i in range(0, len(rel) - 8, 4) if rel[i:i + 4] == MODULE_MAGIC]
    if len(hits) != 1:
        raise SystemExit(f"error: found {len(hits)} module descriptions, expected 1")
    (version,) = struct.unpack_from(">I", rel, hits[0] + 4)
    struct.pack_into(">I", rel, hits[0] + 4, version + 1)
    return version


def parse_module(text):
    try:
        kind, mid, path = text.split(":", 2)
        return kind, KINDS[kind], int(mid), path
    except (ValueError, KeyError):
        raise SystemExit(f"error: --module {text}: expected KIND:ID:FILE with KIND one of {sorted(KINDS)}")


def cmd_pack(a):
    stub = open(a.stub, "rb").read()
    sh = rel_header(stub, a.stub)
    check_fixed(stub, sh, a.stub)
    budgets = {}
    for b in a.budget:
        kind, _, value = b.partition("=")
        budgets[kind] = int(value, 0)
    far_calls = set(a.far_calls)

    out = bytearray(stub)
    out += bytes(align(len(out)) - len(out))
    out += bytes(align(a.pad))
    table, lines, failed, note = [], [], False, ""
    max_temp, max_temp_kind = 0, ""
    parsed = [parse_module(text) for text in a.module]
    kind_of = {mid: kind for kind, _, mid, _ in parsed}
    kind_of[sh["id"]] = "stub"
    for i, (kind, tag, mid, path) in enumerate(parsed):
        rel = bytearray(open(path, "rb").read())
        h = rel_header(rel, path)
        if h["id"] != mid:
            raise SystemExit(f"error: {path} has module id {h['id']}, expected {mid}")
        deps = sorted({kind_of[d] for d in imported_ids(rel, h) if d != mid and d in kind_of})
        bad = [d for d in deps if d not in MAY_IMPORT.get(kind, ())]
        if bad:
            raise SystemExit(f"error: the {kind} module imports from the {', '.join(bad)} module, "
                             "which the stub does not keep loaded for it")
        if h["align"] > 32 or h["bss_align"] > 32:
            raise SystemExit(f"error: {path} needs alignment above 32, which the loader does not give")
        check_fixed(rel, h, path)
        if kind == "extra":
            match_ids = [m_id for m_kind, _, m_id, _ in parsed if m_kind == "match"]
            if match_ids:
                check_extra_thunks(rel, h, path, match_ids[0])
        tramp_note = ""
        if kind in far_calls:
            rel, h, n_stubs, n_sites = synthesize_far_trampolines(rel, h, path)
            tramp_note = f"; {n_stubs} far-call stubs ({n_stubs * STUB_BYTES} B) for {n_sites} sites, 0 external REL24"
        target = kind == a.break_module if a.break_module else i == 0
        if target and a.break_kind == "badversion":
            note = f" (interface version {skew_version(rel)} reported as one higher)"
        rel += bytes(align(len(rel)) - len(rel))
        checksum = fnv1a(rel)
        if target and a.break_kind == "corrupt":
            at = code_offset(h)
            rel[at] ^= 0xFF
            note = f" (byte 0x{at:x} of the {kind} module flipped after checksumming)"
        table.append(ENTRY.pack(mid, tag, len(out), len(rel), checksum, 0, 0, 0))
        out += rel
        if len(rel) > max_temp:
            max_temp, max_temp_kind = len(rel), kind
        footprint = align(h["fix_size"]) + align(h["bss_size"])
        budget = budgets.get(kind, 0)
        lines.append(f"size    {kind} module {len(rel)} bytes, fixSize 0x{h['fix_size']:x} + .bss 0x{h['bss_size']:x}: "
                     f"{footprint} bytes loaded" + (f", budget {budget}" if budget else "")
                     + (f"; links against the {', '.join(deps)} module" if deps else "")
                     + tramp_note)
        if budget and footprint > budget:
            print(f"error: the {kind} module is {footprint - budget} bytes over its budget", file=sys.stderr)
            failed = True
    table_offset = len(out)
    out += b"".join(table)
    out += struct.pack(">8I", TRAILER_MAGIC, FORMAT_VERSION, len(table), table_offset, 0, 0, 0, 0)
    if a.pad:
        note += f" (0x{align(a.pad):x} bytes of padding before the modules)"
    if a.break_kind == "truncated":
        out = out[:len(out) - TRAILER_SIZE - 0x1000]
        note = " (cut 0x1020 bytes short)"

    # gfModule::create keeps fixSize + .bss in the heap; the relocation data
    # past fixSize is read into a temporary buffer (Syriinge's plugin.cpp).
    stub_footprint = align(sh["fix_size"]) + align(sh["bss_size"])
    print(f"size    stub {len(stub)} bytes, fixSize 0x{sh['fix_size']:x} + .bss 0x{sh['bss_size']:x}: "
          f"{stub_footprint} bytes loaded" + (f", budget {a.stub_budget}" if a.stub_budget else ""))
    for line in lines:
        print(line)
    print(f"size    load temp buffer {max_temp} bytes ({max_temp_kind} module)"
          + (f", budget {a.temp_budget}" if a.temp_budget else ""))
    print(f"size    {a.output}: {len(out)} bytes, {len(table)} module(s){note}")
    if a.stub_budget and stub_footprint > a.stub_budget:
        print(f"error: the stub is {stub_footprint - a.stub_budget} bytes over its budget", file=sys.stderr)
        failed = True
    if a.temp_budget and max_temp > a.temp_budget:
        print(f"error: load temp buffer ({max_temp_kind} module) is {max_temp - a.temp_budget} bytes over its budget",
              file=sys.stderr)
        failed = True
    if failed:
        return 1
    with open(a.output, "wb") as f:
        f.write(out)
    return 0


def cmd_info(a):
    data = open(a.file, "rb").read()
    sh = rel_header(data, "the stub")
    print(f"file {len(data)} bytes; stub id {sh['id']}, fixSize 0x{sh['fix_size']:x}")
    magic, fmt, count, table = struct.unpack_from(">4I", data, len(data) - TRAILER_SIZE)
    if magic != TRAILER_MAGIC:
        print("no trailer")
        return 1
    ok = True
    for i in range(count):
        mid, tag, offset, size, checksum = ENTRY.unpack_from(data, table + i * ENTRY.size)[:5]
        rel = data[offset:offset + size]
        h = rel_header(rel, f"module {i}")
        good = fnv1a(rel) == checksum
        ext_rel24 = count_external_rel24(rel, h, f"module {i}")
        ok = ok and good
        print(f"module {KIND_NAMES.get(tag, hex(tag))}: id {mid} (header {h['id']}) at 0x{offset:x}, "
              f"{size} bytes, .bss 0x{h['bss_size']:x}, ext_rel24 {ext_rel24}, "
              f"checksum {checksum:08x} ({'ok' if good else 'MISMATCH'})")
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("pack")
    p.add_argument("--stub", required=True)
    p.add_argument("--module", action="append", required=True, help="KIND:ID:FILE.rel")
    p.add_argument("--stub-budget", type=int, default=0)
    p.add_argument("--temp-budget", type=int, default=0)
    p.add_argument("--budget", action="append", default=[], help="KIND=BYTES")
    p.add_argument("--far-calls", action="append", default=[], choices=sorted(KINDS),
                   help="synthesize MEM2 -> MEM1 far-call trampolines for KIND")
    p.add_argument("--break", dest="break_kind", choices=("badversion", "corrupt", "truncated"))
    p.add_argument("--break-module", default="", help="KIND of the module --break damages (default: the first)")
    p.add_argument("--pad", type=lambda s: int(s, 0), default=0, help="BYTES of zeros before the modules")
    p.add_argument("-o", "--output", required=True)
    i = sub.add_parser("info")
    i.add_argument("file")
    a = ap.parse_args()
    return cmd_pack(a) if a.cmd == "pack" else cmd_info(a)


if __name__ == "__main__":
    sys.exit(main())
