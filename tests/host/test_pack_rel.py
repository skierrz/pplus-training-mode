"""Unit tests for scripts/pack-rel.py far-call trampoline synthesis and temp-budget checks.

Run automatically by tests/host/build.py (`make check-host`).
"""
import importlib.util
import os
import struct
import sys
import tempfile

sys.dont_write_bytecode = True

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PACK_PATH = os.path.join(ROOT, "scripts", "pack-rel.py")
spec = importlib.util.spec_from_file_location("pack_rel", PACK_PATH)
P = importlib.util.module_from_spec(spec)
spec.loader.exec_module(P)


def build_synthetic_rel(mod_id, text_bytes, data_bytes, bss_size, imp_relocs):
    """Build a valid REL v3 image with sections:
      0: null
      1: .text (exec)
      2: .data
      3: .bss (offset 0, size bss_size)
    imp_relocs: [(import_mid, [(cur_sec, cur_off, rtype, target_sec, addend), ...])]
    """
    num_sections = 4
    out = bytearray(P.REL_HEADER.size)
    sec_info = len(out)
    out += bytes(8 * num_sections)

    # Section 1: .text (32-byte aligned)
    out += bytes(P.align(len(out), 32) - len(out))
    text_off = len(out)
    out += text_bytes

    # Section 2: .data (8-byte aligned)
    out += bytes(P.align(len(out), 8) - len(out))
    data_off = len(out)
    out += data_bytes

    # Import table
    out += bytes(P.align(len(out), 4) - len(out))
    imp_off = len(out)
    imp_size = 8 * len(imp_relocs)
    out += bytes(imp_size)

    last = [i for i, (mid, _) in enumerate(imp_relocs) if mid in (0, mod_id)]
    first_dropped = last[0] if last else len(imp_relocs)

    rel_offsets = [0] * len(imp_relocs)
    for i in range(first_dropped):
        rel_offsets[i] = len(out)
        out += P.encode_relocs(imp_relocs[i][1])

    fix_size = len(out)

    for i in range(first_dropped, len(imp_relocs)):
        rel_offsets[i] = len(out)
        out += P.encode_relocs(imp_relocs[i][1])

    rel_off = rel_offsets[0] if rel_offsets else fix_size
    P.REL_HEADER.pack_into(
        out, 0,
        mod_id, 0, 0, num_sections, sec_info, 0, 0,
        3, bss_size, rel_off, imp_off, imp_size,
        1, 1, 1, 3,
        0, 4, 8, 32, 32, fix_size
    )
    secs = [(0, 0), (text_off | 1, len(text_bytes)), (data_off, len(data_bytes)), (0, bss_size)]
    for s, (o, sz) in enumerate(secs):
        struct.pack_into(">2I", out, sec_info + 8 * s, o, sz)
    for i, (mid, _) in enumerate(imp_relocs):
        struct.pack_into(">2I", out, imp_off + 8 * i, mid, rel_offsets[i])
    return out


def signed26(word):
    raw = word & 0x03FFFFFC
    return raw - 0x04000000 if (raw & 0x02000000) else raw


def test_trampoline_synthesis_and_nop_spacers():
    # Make .text > 132 KiB (0x22000 bytes) so the distance from offset 0x10 to
    # the appended trampoline island exceeds 2 * 0xFFFF and requires R_DOLPHIN_NOP spacers.
    text_len = 0x22000
    text = bytearray(b"\x60\x00\x00\x00" * (text_len // 4))  # nop fill
    # Call sites:
    #   +0x0010: bl  (LK=1) -> match (21582) sec 1 +0x100
    #   +0x0014: b   (LK=0) -> match (21582) sec 1 +0x100  (shared stub!)
    #   +0x0020: bl  (LK=1) -> sora_melee (27) sec 1 +0x2000
    #   +0x12000: bl (LK=1) -> DOL (0) 0x80005554
    #   +0x12004: b  (LK=0) -> DOL (0) 0x803F12EC (_restgpr tail-call)
    #   +0x21FF0: bl (LK=1) -> DOL (0) 0x80005554 (shared stub!)
    struct.pack_into(">I", text, 0x0010, 0x48000001)  # bl 0
    struct.pack_into(">I", text, 0x0014, 0x48000000)  # b  0
    struct.pack_into(">I", text, 0x0020, 0x48000001)  # bl 0
    struct.pack_into(">I", text, 0x12000, 0x48000001)  # bl 0
    struct.pack_into(">I", text, 0x12004, 0x48000000)  # b  0
    struct.pack_into(">I", text, 0x21FF0, 0x48000001)  # bl 0

    data = bytearray(b"\x11\x22\x33\x44" * 4)
    mod_id = 21584
    imp_relocs = [
        (27, [
            (1, 0x0020, P.R_PPC_REL24, 1, 0x2000),
            (2, 0x0000, 1, 4, 0x80),  # R_PPC_ADDR32 in .data (must stay untouched)
        ]),
        (21582, [
            (1, 0x0010, P.R_PPC_REL24, 1, 0x100),
            (1, 0x0014, P.R_PPC_REL24, 1, 0x100),
        ]),
        (0, [
            (1, 0x12000, P.R_PPC_REL24, 0, 0x80005554),
            (1, 0x12004, P.R_PPC_REL24, 0, 0x803F12EC),
            (1, 0x21FF0, P.R_PPC_REL24, 0, 0x80005554),
        ]),
        (mod_id, [
            (2, 0x0004, 1, 1, 0x0010),  # self reloc (must stay untouched)
        ]),
    ]

    rel = build_synthetic_rel(mod_id, text, data, 0x40, imp_relocs)
    h = P.rel_header(rel, "synth")
    P.check_fixed(rel, h, "synth")
    assert P.count_external_rel24(rel, h, "synth") == 6

    out, nh, n_stubs, n_sites = P.synthesize_far_trampolines(rel, h, "synth")
    assert n_sites == 6, f"expected 6 rewritten sites, got {n_sites}"
    assert n_stubs == 4, f"expected 4 unique stubs, got {n_stubs}"
    assert P.count_external_rel24(out, nh, "synth") == 0
    P.check_fixed(out, nh, "synth")

    # Section 1 grew by exactly 4 * 16 = 64 bytes; .data stayed identical.
    t_off, t_size = nh["sections"][1]
    assert (t_off & 1) == 1
    t_base = t_off & ~1
    assert t_size == text_len + 4 * P.STUB_BYTES
    d_off, d_size = nh["sections"][2]
    assert out[d_off:d_off + d_size] == data

    # Verify every stub contains lis r12,0; ori r12,r12,0; mtctr r12; bctr.
    for i in range(4):
        s_at = t_base + text_len + i * P.STUB_BYTES
        assert out[s_at:s_at + P.STUB_BYTES] == P.STUB_TEMPLATE

    # Verify local branches preserve LK bit and target the right deduplicated stubs.
    # Stub allocation order follows import order:
    #   stub 0: (27, 1, 0x2000)    @ text_len + 0x00
    #   stub 1: (21582, 1, 0x100)  @ text_len + 0x10
    #   stub 2: (0, 0, 0x80005554) @ text_len + 0x20
    #   stub 3: (0, 0, 0x803F12EC) @ text_len + 0x30
    expected_sites = [
        (0x0020, text_len + 0x00, 1),
        (0x0010, text_len + 0x10, 1),
        (0x0014, text_len + 0x10, 0),
        (0x12000, text_len + 0x20, 1),
        (0x12004, text_len + 0x30, 0),
        (0x21FF0, text_len + 0x20, 1),
    ]
    for site, target, expect_lk in expected_sites:
        (word,) = struct.unpack_from(">I", out, t_base + site)
        assert (word >> 26) == 18 and (word & 2) == 0, f"site 0x{site:x} not relative branch: 0x{word:08x}"
        assert (word & 1) == expect_lk, f"site 0x{site:x} LK mismatch"
        assert site + signed26(word) == target, f"site 0x{site:x} targets 0x{site + signed26(word):x}, want 0x{target:x}"

    # Verify R_DOLPHIN_NOP spacer emission (>64 KiB jump from 0 to 0x22002) and exact HI/LO pairs.
    imps = P.imports(out, nh)
    assert [mid for mid, _ in imps] == [27, 21582, 0, mod_id]
    # Kept imports end <= fix_size; dropped imports start >= fix_size.
    assert imps[0][1] < nh["fix_size"] and imps[1][1] < nh["fix_size"]
    assert imps[2][1] >= nh["fix_size"] and imps[3][1] >= nh["fix_size"]

    # Check raw R_DOLPHIN_NOP entries exist in import 27's relocation list (0x22002 > 2 * 0xFFFF).
    raw_at = imps[0][1]
    nops = 0
    while raw_at + 8 <= len(out):
        delta, rtype, sec, addend = struct.unpack_from(">HBBI", out, raw_at)
        raw_at += 8
        if rtype == P.R_DOLPHIN_END:
            break
        if rtype == P.R_DOLPHIN_NOP:
            assert delta == 0xFFFF and sec == 0 and addend == 0
            nops += 1
    assert nops == 2, f"expected 2 R_DOLPHIN_NOP spacers for offset 0x22002, got {nops}"

    # Decode each import and verify HI/LO records and untouched non-REL24 records.
    rec_27 = P.decode_relocs(out, imps[0][1], "synth")
    assert rec_27 == [
        (1, text_len + 0x02, P.R_PPC_ADDR16_HI, 1, 0x2000),
        (1, text_len + 0x06, P.R_PPC_ADDR16_LO, 1, 0x2000),
        (2, 0x0000, 1, 4, 0x80),
    ], f"unexpected mod 27 relocs: {rec_27}"

    rec_main = P.decode_relocs(out, imps[1][1], "synth")
    assert rec_main == [
        (1, text_len + 0x12, P.R_PPC_ADDR16_HI, 1, 0x100),
        (1, text_len + 0x16, P.R_PPC_ADDR16_LO, 1, 0x100),
    ], f"unexpected mod 21582 relocs: {rec_main}"

    rec_dol = P.decode_relocs(out, imps[2][1], "synth")
    assert rec_dol == [
        (1, text_len + 0x22, P.R_PPC_ADDR16_HI, 0, 0x80005554),
        (1, text_len + 0x26, P.R_PPC_ADDR16_LO, 0, 0x80005554),
        (1, text_len + 0x32, P.R_PPC_ADDR16_HI, 0, 0x803F12EC),
        (1, text_len + 0x36, P.R_PPC_ADDR16_LO, 0, 0x803F12EC),
    ], f"unexpected DOL relocs: {rec_dol}"

    rec_self = P.decode_relocs(out, imps[3][1], "synth")
    assert rec_self == [(2, 0x0004, 1, 1, 0x0010)], f"unexpected self relocs: {rec_self}"


def test_temp_budget_enforcement():
    with tempfile.TemporaryDirectory() as td:
        stub_path = os.path.join(td, "stub.rel")
        extra_path = os.path.join(td, "extra.rel")
        out_path = os.path.join(td, "out.rel")

        text = bytearray(b"\x60\x00\x00\x00" * 32)
        struct.pack_into(">I", text, 0x10, 0x48000001)
        stub_rel = build_synthetic_rel(21581, b"\x4E\x80\x00\x20" * 16, b"\x00" * 16, 0x20, [(0, [])])
        extra_rel = build_synthetic_rel(21584, text, b"\x00" * 16, 0x20, [(0, [(1, 0x10, P.R_PPC_REL24, 0, 0x80004000)])])
        open(stub_path, "wb").write(stub_rel)
        open(extra_path, "wb").write(extra_rel)

        class Args:
            stub = stub_path
            module = [f"extra:21584:{extra_path}"]
            stub_budget = 20480
            temp_budget = 64  # intentionally too small
            budget = ["extra=418816"]
            far_calls = ["extra"]
            break_kind = None
            break_module = ""
            pad = 0
            output = out_path

        assert P.cmd_pack(Args) == 1, "expected pack to fail when module exceeds --temp-budget"
        assert not os.path.exists(out_path)

        Args.temp_budget = 495616
        assert P.cmd_pack(Args) == 0, "expected pack to succeed within --temp-budget"
        assert os.path.exists(out_path)


def main():
    test_trampoline_synthesis_and_nop_spacers()
    test_temp_budget_enforcement()
    print("test_pack_rel: 2/2 PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
