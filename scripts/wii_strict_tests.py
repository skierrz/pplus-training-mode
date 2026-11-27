"""Strict Wii-hardware-compatibility & edge-case verification suite (Wave 3).

Run through emu.py (`make test-wii-strict`):
  test-wii-strict REL [--only NAME ...] [--list] [--interpreter] [--shots]

Scenarios:
  strict-mmu
      Boots and exercises full flow under MMU=True, Fastmem=False,
      PageTableFastmem=False, non-JIT CPUCore (Cached Interpreter=5, or Pure
      Interpreter=0 with --interpreter), css_cache=False:
      boot -> match -> extra MEM2 load -> D-pad savestate save/restore ->
      Stage & Cast > Fighter Swap -> Break the Targets! DVD streaming ->
      clean match/extra exit back to heap baseline.
  strict-dsp-lle
      Boots with MMU=True, Fastmem=False, PageTableFastmem=False, DSPHLE=False
      (`-a LLE`), css_cache=False -> enters Training match -> D-pad savestate
      save & restore -> verifies clock advances smoothly with LLE audio and
      clean match exit.
  strict-4p-heap-frag
      With 4 controllers and a writable SD card holding exported recordings:
      runs 4-player match with PSA Debugger + Action Log active simultaneously,
      performs Fighter Swap costume & character swaps across heavy fighters,
      opens/closes CSS Recordings Browser to exercise MEM2 read buffers, and
      walks g_HeapInfos[Heaps::Network] directly at every checkpoint verifying
      largest contiguous free block >= 0x10BA20 (1,096,224 B).
  strict-ps-zoom
      Enters Powershield drill, toggles Attack Mode (Projectile <-> Physical)
      live via Lab Menu `menu_nav_to("Scenarios > Attack Mode")`, verifies
      TMPD `psResolvedMode` transitions (0 -> 1 -> 0) and live CPU re-anchor
      between laser firing and physical jabs/aerials; then enters solo drill on
      Final Destination (0x02), Battlefield (0x01), and Yoshi's Story (0x2A)
      verifying `soloZoomCam` frames P1 with finite zoomed camera geometry.
"""
import argparse
import os
import re
import struct
import sys
import time

sys.dont_write_bytecode = True

import cpu_tests as CT  # noqa: E402
import emu as E  # noqa: E402
import fighter_set_tests as FS  # noqa: E402
import fighter_swap_tests as FSW  # noqa: E402
import lab_tests as LT  # noqa: E402
import loader_tests as L  # noqa: E402
import memdump  # noqa: E402
import nav  # noqa: E402
import psa_inspector_tests as PSA  # noqa: E402
import record_tests as RT  # noqa: E402
import savestate_tests as SS  # noqa: E402
import skill_drills_ui_tests as SD  # noqa: E402
import stage4a_core_tests as S4  # noqa: E402
import storage_tests as ST  # noqa: E402
import stream3_break_targets_tests as S3  # noqa: E402
from harness import HarnessError, Timeout  # noqa: E402

SCENARIOS = [
    "strict-mmu",
    "strict-dsp-lle",
    "strict-4p-heap-frag",
    "strict-ps-zoom",
]

MIN_COSTUME_FREE = 0x10BA20  # P+'s 0x10BA20 costume decompression contiguous free requirement
HEAP_NETWORK = 6             # sr_common.h Heaps::Network = 0x6
CAMERA_MGR_ADDR = 0x805B6D20 # gfCameraManager::getManager()
SET_ACTION_LOG = 22          # main::Settings::general.actionLog

TMBR_MAGIC = S3.TMBR_MAGIC
TMBR_CMD_OFF = S3.TMBR_CMD_OFF
RUSH_BLOCK_RE = S3.RUSH_BLOCK_RE


class StrictError(RuntimeError):
    pass


def parse_args(rest):
    ap = argparse.ArgumentParser(prog="emu.py test-wii-strict")
    ap.add_argument("rel")
    ap.add_argument("--only", nargs="+", choices=SCENARIOS, default=SCENARIOS)
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--interpreter", action="store_true",
                    help="use CPUCore=0 Pure Interpreter instead of CPUCore=5 Cached Interpreter")
    ap.add_argument("--shots", action="store_true", help="capture D3D screenshots during scenarios")
    return ap.parse_args(rest)


def _network_heap_stats(emu):
    """Returns (largest_free_payload, total_free_payload, free_block_count, allocated_count) for Heaps::Network.
    Mirrors src/shared/game/heap.cpp:heapUsage() by walking gfMemoryPool's sentinel free list directly."""
    name_p, pool, _size, _ = struct.unpack(">IIII", emu.read(memdump.HEAP_INFOS + 16 * HEAP_NETWORK, 16))
    if not emu.is_ram(pool):
        raise StrictError(f"Heaps::Network pool {pool:#x} not in RAM")
    name = emu.cstr(name_p, 24)
    if name != "Network":
        raise StrictError(f"heap slot {HEAP_NETWORK} is {name!r}, expected 'Network'")
    h = emu.read(pool, 0x24)
    start, end = struct.unpack_from(">II", h, 4)
    allocated = struct.unpack_from(">H", h, 0x14)[0]
    sentinel = struct.unpack_from(">I", h, 0x20)[0]
    block = emu.u32(sentinel + 8)
    free_sizes = []
    for _ in range(512):
        if not start <= block < end:
            break
        sz = emu.u32(block + 4)
        if sz < 0x20:
            break
        free_sizes.append(sz - 0x20)
        nxt = emu.u32(block + 8)
        if nxt <= block:
            break
        block = nxt
    largest = max(free_sizes) if free_sizes else 0
    total_free = sum(free_sizes)
    return largest, total_free, len(free_sizes), allocated


def _assert_clean_unload(emu, since_pos, label):
    tail = emu.log_text()[since_pos:]
    bad = L.problems(tail)
    if bad:
        raise StrictError(f"{label}: log problems: {bad[:3]}")
    if "back to baseline: NO" in tail:
        raise StrictError(f"{label}: heap did not return to baseline on unload")


def run_strict_mmu(rel, cpu_core=5):
    print(f"=== [strict-mmu] CPUCore={cpu_core}, MMU=True, Fastmem=False, PageTableFastmem=False ===")
    strict_args = E.DETERMINISTIC_ARGS + [
        "-C", f"Dolphin.Core.CPUCore={cpu_core}",
        "-C", "Dolphin.Core.MMU=True",
        "-C", "Dolphin.Core.Fastmem=False",
        "-C", "Dolphin.Core.PageTableFastmem=False",
    ]
    # Non-JIT boot runs uncached; give ample frame/wall tolerance under Cached/Pure Interpreter.
    with E.harness_run(E.headless_backend(), strict_args, allow_null=True, css_cache=False) as (_proc, emu):
        match_pos = len(emu.log_text())
        nav.boot_to_training_css(emu)
        nav.start_training_match(emu, "mario", "fox", stage="final_destination", seed_rng=True)
        emu.advance(45)

        table = L.heap_table(emu)
        assist = table.get("AssistFigureResource")
        if assist is None or assist[2] < 1:
            raise StrictError(f"strict-mmu: AssistFigureResource missing extra module ({assist})")
        print(f"  extra MEM2 loaded OK: AssistFigureResource={assist}")

        # Save + load savestate via real controller D-pad path under strict MMU.
        gf0 = nav.game_frame(emu)
        slot, err = FS.dpad_save(emu)
        if err is not None:
            raise StrictError(f"strict-mmu: D-pad save failed: {err}")
        emu.advance(25)
        gf1, lerr = FS.dpad_load(emu)
        if lerr is not None or gf1 < gf0:
            raise StrictError(f"strict-mmu: D-pad load failed: gf={gf1}, err={lerr}")
        emu.advance(15)
        gf2 = nav.game_frame(emu)
        if gf2 <= gf1:
            raise StrictError(f"strict-mmu: clock did not advance after savestate load ({gf1} -> {gf2})")
        print(f"  savestate D-pad save/restore OK (slot {slot}, frame {gf1} -> {gf2})")

        # Fighter Swap via Stage & Cast > Fighter Swap.
        sb = FSW.SwapBlock(emu)
        sb.find()
        sw = FSW.apply_swap_via_menu(emu, sb, p1="Peach", p1_cos=0)
        if sw["completedCount"] < 1:
            raise StrictError(f"strict-mmu: fighter swap did not complete ({sw})")
        print(f"  fighter swap OK (Mario -> Peach, completed={sw['completedCount']})")

        # Break the Targets! DVD target texture streaming (`STGTARGET`).
        tmbr_addr = S3.find_block(emu, RUSH_BLOCK_RE, match_pos, "TMBR")
        emu.h.call("write_mem", addr=tmbr_addr + TMBR_CMD_OFF, hex="03")
        t_st = None
        for _ in range(90):
            emu.advance(2)
            t_st = S3.read_tmbr(emu, tmbr_addr)
            if t_st["active"] == 1 and t_st["texLoaded"] == 1 and t_st["activeTargets"] >= 3:
                break
        if not t_st or t_st["active"] != 1 or t_st["texLoaded"] != 1:
            raise StrictError(f"strict-mmu: BTT DVD stream failed ({t_st})")
        emu.advance(30)
        print(f"  BTT DVD stream OK (texLoaded={t_st['texLoaded']}, targets={t_st['activeTargets']})")

        nav.finish_training(emu)
        emu.advance(30)
        _assert_clean_unload(emu, match_pos, "strict-mmu")
    print("strict-mmu: PASS")


def run_strict_dsp_lle(rel, cpu_core=5):
    print(f"=== [strict-dsp-lle] CPUCore={cpu_core}, MMU=True, Fastmem=False, PageTableFastmem=False, DSPHLE=False (-a LLE) ===")
    lle_args = E.DETERMINISTIC_ARGS + [
        "-a", "LLE",
        "-C", f"Dolphin.Core.CPUCore={cpu_core}",
        "-C", "Dolphin.Core.MMU=True",
        "-C", "Dolphin.Core.Fastmem=False",
        "-C", "Dolphin.Core.PageTableFastmem=False",
        "-C", "Dolphin.Core.DSPHLE=False",
    ]
    with E.harness_run(E.headless_backend(), lle_args, allow_null=True, css_cache=False) as (_proc, emu):
        match_pos = len(emu.log_text())
        nav.boot_to_training_css(emu)
        nav.start_training_match(emu, "mario", "fox", stage="final_destination", seed_rng=True)
        emu.advance(45)

        slot, err = FS.dpad_save(emu)
        if err is not None:
            raise StrictError(f"strict-dsp-lle: D-pad save failed: {err}")
        emu.advance(30)
        gf_loaded, lerr = FS.dpad_load(emu)
        if lerr is not None:
            raise StrictError(f"strict-dsp-lle: D-pad load failed: {lerr}")
        emu.advance(45)
        gf_after = nav.game_frame(emu)
        if gf_after <= gf_loaded + 20:
            raise StrictError(f"strict-dsp-lle: match hung after LLE savestate restore ({gf_loaded} -> {gf_after})")
        print(f"  DSP LLE savestate restore OK (slot {slot}, frame {gf_loaded} -> {gf_after})")

        nav.finish_training(emu)
        emu.advance(30)
        _assert_clean_unload(emu, match_pos, "strict-dsp-lle")
    print("strict-dsp-lle: PASS")


def run_strict_4p_heap_frag(rel):
    print("=== [strict-4p-heap-frag] 4P + PSA + ActionLog + Costume Swaps + Browser (largest >= 0x10BA20) ===")
    img = E.storage_image(rel)
    try:
        with E.harness_run(E.headless_backend(), E.FOUR_PADS_ARGS, allow_null=True,
                           writable_image=img, locked=False) as (_proc, emu):
            nav.boot_to_training_css(emu)
            nav.start_training_match(emu, "mario", "fox", stage="battlefield", seed_rng=True)
            emu.advance(30)

            lab = LT.Lab(emu, [])
            settings = lab.find_settings()
            # Export a recording first (1P+1CPU) so Browser has a real file to list and read into Heaps::Network.
            r = RT.Runner(emu)
            r.refresh()
            emu.advance(20)
            r.run_request("save")
            RT.record_hmn(r)
            emu.h.call("resume")
            nav.pause_training(emu)
            lab.open_menu()
            lab.menu_nav_to("Recording > Export Recording")
            pos_exp = lab.log_pos()
            lab.tap(["A"])
            ST.wait_line(emu, "strict-4p export", r"storage: exported \S+", pos_exp, 300)
            lab.close_menu()
            emu.advance(15)

            nav.set_training_cpus(emu, 3)
            emu.advance(20)

            # Turn on PSA Inspector + Action Log together during 4P gameplay.
            emu.h.call("write_mem", addr=settings + PSA.S_PSA_INSPECTOR, hex="01")
            emu.h.call("write_mem", addr=settings + SET_ACTION_LOG, hex="01")
            emu.advance(45)

            def check_heap(step_name):
                largest, total_free, nfree, allocs = _network_heap_stats(emu)
                print(f"  [{step_name}] Heaps::Network: largest={largest:#x} (min {MIN_COSTUME_FREE:#x}), "
                      f"free={total_free:#x} in {nfree} block(s), alloc={allocs}")
                if largest < MIN_COSTUME_FREE:
                    raise StrictError(f"strict-4p-heap-frag ({step_name}): largest free {largest:#x} < {MIN_COSTUME_FREE:#x}")

            check_heap("4p-psa-actionlog-active")

            # Perform Fighter Swap costume + heavy character swaps in 4P match with PSA & ActionLog active
            # via live RAM label lookup (`menu_nav_to`), avoiding hardcoded category/row indices.
            sb = FSW.SwapBlock(emu)
            sb.find()
            for p1_name, p1_cos, cpu_name, cpu_cos in (
                ("Ice Climbers", 1, "Dedede", 2),
                ("Peach", 3, "Kirby", 1),
                ("Olimar", 2, "Samus", 3),
            ):
                d_before = sb.read()
                lab.open_menu(keep=True)
                lab.menu_nav_to("Stage & Cast > Fighter Swap > Apply Swap")
                sb.write_scratch(FSW.ROSTER_IDX[p1_name], p1_cos, FSW.ROSTER_IDX[cpu_name], cpu_cos)
                lab.tap(["A"])
                sw = FSW.wait_swap_done(emu, sb, d_before["completedCount"])
                emu.advance(20)
                if sw["refusalCount"] != 0:
                    raise StrictError(f"strict-4p-heap-frag: swap refused ({sw})")
                check_heap(f"post-swap-{p1_name}-c{p1_cos}")

            # Exit to CSS, open & scan Recordings Browser (uses Heaps::Network buffers), close & re-enter 4P match.
            pos_exit = len(emu.log_text())
            nav.finish_training(emu)
            emu.advance(30)
            _assert_clean_unload(emu, pos_exit, "strict-4p-exit1")
            check_heap("css-post-exit1")

            cnt, _ = ST.open_browser(emu)
            if cnt < 1:
                raise StrictError("strict-4p-heap-frag: expected exported recording in Browser")
            emu.advance(30)
            check_heap("css-browser-open")
            emu.press(0, ["B"], frames=4, after=12)
            check_heap("css-browser-closed")

            pos_m2 = len(emu.log_text())
            nav.start_training_match(emu, "mario", "fox", stage="final_destination", seed_rng=True)
            emu.advance(30)
            nav.set_training_cpus(emu, 3)
            emu.advance(30)
            check_heap("4p-match2-with-psa-actionlog")
            nav.finish_training(emu)
            emu.advance(30)
            _assert_clean_unload(emu, pos_m2, "strict-4p-exit2")
    finally:
        E.remove_storage_image(img)
    print("strict-4p-heap-frag: PASS")


def run_strict_ps_zoom(rel, take_shots=False):
    print("=== [strict-ps-zoom] Live Powershield Projectile <-> Physical toggle & soloZoomCam ===")
    backend = "D3D" if take_shots else E.headless_backend()
    with E.harness_run(backend, E.DETERMINISTIC_ARGS, allow_null=(not take_shots)) as (_proc, emu):
        # 1. Powershield Projectile <-> Physical live menu toggle (`menu_nav_to("Drill > Attack Mode")`).
        class Helper(SD.Runner):
            pass

        t = Helper(emu)
        t.ensure_page_open("Tech Practice")
        _p1, cpu, _tmdh = t.arm_skill_drill_and_enter(3, 5, "mario", "fox", aspect="16:9", freeze_cpu=False)
        emu.h.call("pause")
        emu.h.call("frame_advance", n=2)
        p0 = SD.read_tmpd(emu, t.tmpd)
        if p0["activeDrill"] != 5 or p0["psResolvedMode"] != 0:
            raise StrictError(f"strict-ps-zoom: expected Powershield Projectile (0), got {p0}")

        # Toggle Attack Mode Projectile (0) -> Physical (1) via live Lab Menu navigation.
        emu.h.call("resume")
        lab = LT.Lab(emu, [])
        lab.find_settings()
        lab.open_menu()
        lab.menu_nav_to("Scenarios > Attack Mode")
        lab.tap(stick=LT.RIGHT)
        lab.close_menu()
        emu.h.call("pause")
        emu.h.call("frame_advance", n=4)
        p_phys = SD.read_tmpd(emu, t.tmpd)
        if p_phys["psResolvedMode"] != 1:
            raise StrictError(f"strict-ps-zoom: Attack Mode did not switch to Physical: {p_phys}")

        # Step 150 frames and verify Fox CPU switches to physical grounded attacks (status 0x24..0x33, not SpecialN).
        cpu_ptr = cpu["fighter"]
        phys_attacks, lasers = 0, 0
        for _ in range(150):
            emu.h.call("frame_advance", n=1)
            cp = S4.read_fighter_full(emu, cpu_ptr)
            if 0x24 <= cp["status"] <= 0x33:  # Attack .. AttackAir (status_ids.h)
                phys_attacks += 1
            elif cp["status"] >= 0x112:
                lasers += 1
        if phys_attacks == 0 or lasers > 0:
            raise StrictError(f"strict-ps-zoom: Physical mode wrong CPU moves (phys={phys_attacks}, lasers={lasers})")
        print(f"  Powershield Physical toggle OK (physFrames={phys_attacks}, lasers={lasers})")

        # Toggle back Physical (1) -> Projectile (0) and verify lasers resume.
        emu.h.call("resume")
        lab.open_menu()
        lab.menu_nav_to("Scenarios > Attack Mode")
        lab.tap(stick=LT.LEFT)
        lab.close_menu()
        emu.h.call("pause")
        emu.h.call("frame_advance", n=4)
        p_proj2 = SD.read_tmpd(emu, t.tmpd)
        if p_proj2["psResolvedMode"] != 0:
            raise StrictError(f"strict-ps-zoom: Attack Mode did not switch back to Projectile: {p_proj2}")
        lasers2 = 0
        for _ in range(150):
            emu.h.call("frame_advance", n=1)
            if S4.read_fighter_full(emu, cpu_ptr)["status"] >= 0x112:
                lasers2 += 1
        if lasers2 == 0:
            raise StrictError("strict-ps-zoom: Fox CPU did not resume firing lasers after Physical -> Projectile")
        print(f"  Powershield Projectile restore OK (laserFrames={lasers2})")
        emu.h.call("resume")
        nav.finish_training(emu)
        emu.advance(15)

        # 2. soloZoomCam check across Final Destination, Battlefield, and Yoshi's Story.
        import drill_tests as DT
        for s_name in ("final_destination", "battlefield", "yoshis_story"):
            t.ensure_page_open("Tech Practice")
            DT.go_to_row(emu, 1)
            emu.advance(4)
            emu.press(0, ["A"], frames=4, after=8)
            emu.wait_for("wavedash drill armed", lambda: nav.drill_list_state(emu) == "closed", 120, every=2)
            mpos = len(emu.log_text())
            nav.start_training_match(emu, "fox", "mario", stage=s_name, seed_rng=True)
            emu.advance(40)
            tmpd_a = SD.find_tmpd(emu, mpos)
            pd = SD.read_tmpd(emu, tmpd_a)
            if pd["activeDrill"] != 2 or pd["cpuInStandby"] != 1 or pd["wdZoneActive"] != 0:
                raise StrictError(f"strict-ps-zoom ({s_name}): expected Wavedash soloZoomCam standby, got {pd}")
            cam_raw = emu.read(CAMERA_MGR_ADDR + 0x60, 0x18)
            tx, ty, tz, cx, cy, cz = struct.unpack(">6f", cam_raw)
            # Brawl's tele-FOV places zoomed 1P soloZoomCam around Z=180..290 focused near P1 (vs unzoomed 320..480).
            if not (80.0 < cz < 340.0) or any(v != v or abs(v) > 2000.0 for v in (tx, ty, tz, cx, cy, cz)):
                raise StrictError(f"strict-ps-zoom ({s_name}): implausible soloZoomCam target={(tx, ty, tz)} eye={(cx, cy, cz)}")
            if take_shots:
                emu.screenshot(f"strict-solocam-{s_name}")
            print(f"  soloZoomCam {s_name}: camera target=({tx:.1f},{ty:.1f},{tz:.1f}) eye=({cx:.1f},{cy:.1f},{cz:.1f})")
            nav.finish_training(emu)
            emu.advance(15)
    print("strict-ps-zoom: PASS")


def cmd_test_wii_strict(rest):
    opts = parse_args(rest)
    if opts.list:
        for name in SCENARIOS:
            print(name)
        return 0
    t0 = time.monotonic()
    for sc in opts.only:
        if sc == "strict-mmu":
            run_strict_mmu(opts.rel, cpu_core=(0 if opts.interpreter else 5))
        elif sc == "strict-dsp-lle":
            run_strict_dsp_lle(opts.rel, cpu_core=(0 if opts.interpreter else 5))
        elif sc == "strict-4p-heap-frag":
            run_strict_4p_heap_frag(opts.rel)
        elif sc == "strict-ps-zoom":
            run_strict_ps_zoom(opts.rel, take_shots=opts.shots)
    print(f"test-wii-strict: ALL {len(opts.only)} scenarios PASS in {time.monotonic() - t0:.1f}s")
    return 0
