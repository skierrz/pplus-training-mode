"""Run the plugin in Dolphin: SD folder setup, install, interactive run, tests.

usage: emu.py sd-folder
       emu.py install REL
       emu.py run [REL]
       emu.py test-load REL
       emu.py test-match REL [--p1 NAME] [--p1-costume N] [--cpu NAME]
                             [--stage NAME] [--cpus N] [--without-plugin]
       emu.py test-stress REL [--cycles N] [--cpus N] [--items] [--versus]
       emu.py test-reject BROKEN_REL KIND --restore REL
       emu.py test-lab REL [--presses N]
       emu.py test-displays REL [--only NAME ...]
       emu.py test-osds REL [--only NAME ...]
       emu.py test-drills REL [--cycles N]
       emu.py test-cpu REL [--only NAME ...] [--shard I/N] [--list]
       emu.py test-recovery-combo REL [--only NAME ...] [--list]
       emu.py test-display REL [--only NAME ...]
       emu.py test-controls REL [--only NAME ...] [--keep-going]
       emu.py test-di REL [--only NAME ...] [--measure]
       emu.py test-frames REL [--chars NAME ...] [--only NAME ...] [--keep-going]
       emu.py test-savestate REL [--long] [--only NAME ...] [--cycles N]
                                 [--hmn NAME] [--cpu NAME] [--stage NAME] [--sweep NAME ...]
       emu.py test-charges REL [--only NAME ...] [--keep-going]
       emu.py test-record REL [--only NAME ...] [--hmn NAME] [--cpu NAME] [--stage NAME]
       emu.py test-storage REL [--only NAME ...]
       emu.py test-card CARD [--rel REL] [--ref REF] [--only NAME ...] [--keep-copy]
       emu.py test-sounds REL
       emu.py test-rng REL [--only NAME ...] [--keep-going] [--dump PREFIX]
       emu.py test-match-hud REL [--only NAME ...] [--aspect 16:9|4:3]
       emu.py test-code-menu REL [--only NAME ...] [--aspect 16:9|4:3] [--list]
       emu.py test-harness REL [--bench] [--dual-core]
       emu.py compare-shots A.png B.png
       emu.py self-check

test-match, test-stress and test-reject live in loader_tests.py, test-lab in
lab_tests.py, test-displays in display_tests.py, test-drills in drill_tests.py,
test-cpu in cpu_tests.py, test-display in display_options_tests.py,
test-controls in control_tests.py, test-di in di_tests.py, test-frames in
frame_tests.py, test-savestate in savestate_tests.py, test-record in
record_tests.py, test-storage in storage_tests.py, test-card in card_tests.py,
test-sounds in sound_tests.py, test-rng in rng_tests.py,
match_hud_tests.py, test-harness in harness_tests.py.

Settings come from environment variables (the Makefile exports them from
local.mk), so paths with spaces need no shell quoting:

  DOLPHIN       Dolphin.exe to run
  DOLPHIN_USER  Dolphin user folder for this project only (created if missing)
  LAUNCHER_DOL  Project+ Offline Launcher DOL (booted with -e)
  BRAWL_ISO     Brawl disc image (RSBE01), set as Dolphin's default ISO
  SD_SOURCE     clean Project+ SD card image (read only; for sd-folder)
  SD_FOLDER     folder Dolphin packs into the SD image at every boot
  SD_IMAGE      SD image Dolphin generates from SD_FOLDER
  NAND_SEED     optional folder copied once into the user folder's Wii/
                (e.g. one holding title/00010000/52534245 with a Brawl save)
  TEST_TIMEOUT  seconds test-load waits without a new log line (default 600)
  SCREENSHOT_DIR  where the tests save screenshots
                (default: the user folder's ScreenShots)
  VIDEO_BACKEND video backend (default D3D for screenshot tests, Null for
                suites that do not check screenshots; set D3D to capture
                their attachment or failure screenshots)
  HARNESS_PORT  harness port on 127.0.0.1 (default: a free one)
  EMU_SPEED     Dolphin's speed limit for a harness test: 1 real time, 0 no
                limit (default: the test's own, see speed_args)
  EMU_CSS_STATE per-build Training CSS savestate cache: 1 enabled (default),
                0 disabled (always cold-boot)
  EMU_HOLD_FILE optional path; no emulator starts while that file exists
  EMU_MIN_FREE_MB       minimum free physical memory in MB before launch
                        (default 1500; 0 disables)
  EMU_MIN_FREE_DISK_MB  minimum free disk space in MB on the sandbox drive
                        before launch (default 6000 when copying a card image,
                        1000 otherwise; 0 disables)
  HARNESS_BATCH, HARNESS_AUDIT, HARNESS_PROFILE  see harness.py

Only the Dolphin process this script starts is ever stopped.
"""
import contextlib
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import time

sys.dont_write_bytecode = True  # no __pycache__ in the repo

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
if HERE not in sys.path:
    sys.path.insert(0, HERE)

from harness import Emu, Harness, HarnessError, Timeout, free_port, rotated_log_path  # noqa: E402
import nav  # noqa: E402

# The card P+ ships is SDSC (under 2 GiB). Dolphin sizes a packed image as the
# content plus 512 MiB-8 GiB of free space, which for P+ is over 2 GiB and so
# would be presented as an SDHC card. Pin the size to the SDSC maximum.
SD_IMAGE_BYTES = 2 * 1024 * 1024 * 1024

# Keys this script owns in the user folder's config; set before every launch.
DOLPHIN_INI = {
    "Analytics": {"Enabled": "False", "PermissionAsked": "True"},
    "AutoUpdate": {"CheckForUpdates": "False"},
    "Interface": {"ConfirmStop": "False"},
    "Display": {"Fullscreen": "False"},
    "Core": {
        "WiiSDCard": "True",
        "WiiSDCardEnableFolderSync": "True",
        # The folder is the source of truth: the game cannot change the card,
        # and Dolphin does not unpack the image back into the folder on exit.
        "WiiSDCardAllowWrites": "False",
        "WiiSDCardFilesize": str(SD_IMAGE_BYTES),
    },
}
# Graphics: compile shaders synchronously, so no frame is drawn with objects
# missing while their shaders compile (the fork defaults to "Asynchronous
# (Skip Drawing)", key and values from its Core/Config/GraphicsSettings.cpp).
GFX_INI = {"Settings": {"ShaderCompilationMode": "0"}}
LOGGER_INI = {
    "Options": {"WriteToFile": "True", "WriteToConsole": "False", "WriteToWindow": "False",
                "Verbosity": "4"},
    "Logs": {"OSREPORT": "True", "OSREPORT_HLE": "True", "BOOT": "True", "MASTER": "True",
             "COMMON": "True", "CORE": "True",
             # Logs every SD block read at Verbosity 4; turn on to debug SD access.
             "IOS_SD": "False"},
}

# Unattended run: no audio, no window, panics logged instead of shown.
QUIET_ARGS = [
    "-b", "--render",
    "-C", "Dolphin.DSP.Backend=No Audio Output",
    "-C", "Dolphin.DSP.Muted=True",
    "-C", "Dolphin.Interface.UsePanicHandlers=False",
    # Port 1 is a standard controller driven by the harness, and only harness
    # input reaches the game. Every unattended run also writes all four
    # ports into the config (see pad_ports and check_pads): an override
    # alone leaves the ini value, and this Dolphin opens the GameCube
    # adapter if either is the adapter.
    "-C", "Dolphin.Core.SIDevice0=6",
    "-C", "Dolphin.Input.BackgroundInput=False",
]
TEST_ARGS = QUIET_ARGS + ["-v", "Null"]
# Runs whose screenshots are compared frame for frame: one CPU thread (dual
# core is not deterministic). The random number generators are seeded by the
# test itself (nav.seed_match_rng).
DETERMINISTIC_ARGS = [
    "-C", "Dolphin.Core.CPUThread=False",
]
# Port 2 the only controller (test-boot: the menu goes to it).
PORT2_ONLY_ARGS = ["-C", "Dolphin.Core.SIDevice0=0", "-C", "Dolphin.Core.SIDevice1=6"]
# Ports 2-4 as standard controllers too, for menus that need more players.
FOUR_PADS_ARGS = ["-C", "Dolphin.Core.SIDevice1=6", "-C", "Dolphin.Core.SIDevice2=6",
                  "-C", "Dolphin.Core.SIDevice3=6"]

# SI device values (Dolphin's SIDevices enum): none, standard controller, and
# the GameCube controller adapter. This Dolphin build defaults every port to
# the adapter, and it opens the (single, host-wide) adapter whenever a port's
# effective value *or* its ini value is the adapter, so an unattended run
# that leaves any port unset in the ini takes the owner's real adapter away
# from whatever else is using it. Unattended runs therefore write all four
# ports into the user folder's Dolphin.ini; `make run` writes none.
SI_NONE, SI_STANDARD, SI_ADAPTER = 0, 6, 12
# Wii Remote Source values in WiimoteNew.ini: none, emulated, real Bluetooth.
WIIMOTE_NONE, WIIMOTE_EMULATED, WIIMOTE_REAL = 0, 1, 2


def pad_ports(extra_args):
    """Ini values for ports 1-4 of an unattended run with `extra_args`."""
    if all(a in extra_args for a in PORT2_ONLY_ARGS):
        return [SI_NONE, SI_STANDARD, SI_NONE, SI_NONE]
    four = all(a in extra_args for a in FOUR_PADS_ARGS)
    return [SI_STANDARD] + [SI_STANDARD if four else SI_NONE] * 3


def die(msg):
    print(f"error: {msg}", file=sys.stderr)
    sys.exit(2)


def env(name, required=True):
    value = os.environ.get(name, "").strip()
    if required and not value:
        die(f"{name} is not set; copy local.mk.example to local.mk and fill it in")
    return value


def env_file(name):
    path = env(name)
    if not os.path.isfile(path):
        die(f"{name} does not exist: {path}")
    return os.path.abspath(path)


def shard_groups(units, seconds, n):
    """Divides `units` (each a scenario name or a tuple/list of scenario names
    that share a match or savestate and must stay together, in execution order)
    into `n` consecutive groups of run times as equal as they get: the split
    with the smallest sum of squared group times. `seconds` maps a scenario
    name to its measured run time; a name without one counts as the mean of
    those that have one. Returns `n` flat lists of scenario names: every
    scenario is in exactly one shard, no shard is empty, and every grouped
    unit stays intact in one shard."""
    def as_list(u):
        return [u] if isinstance(u, str) else list(u)

    flat = [name for u in units for name in as_list(u)]
    known = [seconds[x] for x in flat if x in seconds]
    mean = sum(known) / len(known) if known else 1.0
    w = [sum(float(seconds.get(x, mean)) for x in as_list(u)) for u in units]
    total = [0.0]
    for x in w:
        total.append(total[-1] + x)
    # best[k][j]: (cost, start of the last group) for the first j units in k groups.
    best = [[None] * (len(w) + 1) for _ in range(n + 1)]
    best[0][0] = (0.0, 0)
    for k in range(1, n + 1):
        for j in range(k, len(w) + 1):
            best[k][j] = min((best[k - 1][i][0] + (total[j] - total[i]) ** 2, i)
                             for i in range(k - 1, j) if best[k - 1][i] is not None)
    groups, j = [], len(w)
    for k in range(n, 0, -1):
        i = best[k][j][1]
        groups.insert(0, [name for u in units[i:j] for name in as_list(u)])
        j = i
    return groups


def shard(units, seconds, spec):
    """The scenario names that --shard `spec` ("I/N": group I of N, from 1)
    plays; all of them without a spec. See shard_groups."""
    flat = [name for u in units for name in ([u] if isinstance(u, str) else u)]
    if not spec:
        return flat
    m = re.fullmatch(r"(\d+)/(\d+)", spec)
    i, n = (int(m.group(1)), int(m.group(2))) if m else (0, 0)
    if not 1 <= i <= n or n > len(units):
        die(f"--shard takes I/N with 1 <= I <= N <= {len(units)} (the number of shard units), not {spec!r}")
    return shard_groups(units, seconds, n)[i - 1]


def plugins_dir(sd_folder):
    """<sd>/Project+/pf/plugins, matching existing folder names case-insensitively."""
    path = sd_folder
    for part in ("Project+", "pf", "plugins"):
        names = {n.lower(): n for n in os.listdir(path)} if os.path.isdir(path) else {}
        if part.lower() not in names:
            die(f"{os.path.join(path, part)} not found; run `make sd-folder` first")
        path = os.path.join(path, names[part.lower()])
    return path


def update_ini(path, values):
    """Set `values` ({section: {key: value}}) in a Dolphin .ini, keeping everything else."""
    lines = []
    if os.path.exists(path):
        with open(path, encoding="utf-8") as f:
            lines = f.read().splitlines()
    pending = {s: dict(kv) for s, kv in values.items()}
    out, section = [], None

    def flush(sec):
        for k, v in pending.pop(sec, {}).items():
            out.append(f"{k} = {v}")

    for line in lines:
        m = re.match(r"\s*\[(.+)\]\s*$", line)
        if m:
            flush(section)
            section = m.group(1)
            out.append(line)
            continue
        m = re.match(r"\s*([^=]+?)\s*=", line)
        if m and section in pending and m.group(1) in pending[section]:
            out.append(f"{m.group(1)} = {pending[section].pop(m.group(1))}")
            continue
        out.append(line)
    flush(section)
    for sec in list(pending):
        out.append(f"[{sec}]")
        flush(sec)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(out) + "\n")


def low_priority():
    """Popen arguments that start an unattended Dolphin below normal CPU
    priority, so it yields to anything interactive on the machine (the waits
    are counted in game frames, with wall-clock limits generous enough for a
    busy machine). `make run` keeps normal priority."""
    flag = getattr(subprocess, "BELOW_NORMAL_PRIORITY_CLASS", None)
    if os.name == "nt" and flag is not None:
        return {"creationflags": flag}
    if hasattr(os, "nice"):
        return {"preexec_fn": lambda: os.nice(10)}
    return {}


def read_ini_value(path, section, key):
    current = None
    if os.path.exists(path):
        with open(path, encoding="utf-8") as f:
            for line in f:
                m = re.match(r"\s*\[(.+)\]\s*$", line)
                if m:
                    current = m.group(1)
                    continue
                m = re.match(r"\s*([^=]+?)\s*=\s*(.*?)\s*$", line)
                if m and current == section and m.group(1) == key:
                    return m.group(2)
    return None


def check_pads(ini_path, args):
    """Refuse to start an unattended run that could open the GameCube adapter
    or a real Bluetooth Wii Remote: every GameCube port and Wii Remote slot
    must be set in the ini to none or emulated/standard, and no -C override may
    select the adapter or a real Wii Remote."""
    for port in range(4):
        value = read_ini_value(ini_path, "Core", f"SIDevice{port}")
        if value not in (str(SI_NONE), str(SI_STANDARD)):
            die(f"{ini_path}: [Core] SIDevice{port} is {value!r}; an unattended run would open the "
                f"GameCube adapter (this Dolphin's default)")
    for a in args:
        m = re.match(r"Dolphin\.Core\.SIDevice\d=(\d+)$", a)
        if m and int(m.group(1)) not in (SI_NONE, SI_STANDARD):
            die(f"override {a} would open the GameCube adapter")
    wiimote_ini = os.path.join(os.path.dirname(ini_path), "WiimoteNew.ini")
    for slot in range(1, 5):
        value = read_ini_value(wiimote_ini, f"Wiimote{slot}", "Source")
        if value not in (str(WIIMOTE_NONE), str(WIIMOTE_EMULATED)):
            die(f"{wiimote_ini}: [Wiimote{slot}] Source is {value!r}; an unattended run must not open a "
                f"real Wii Remote")
    for a in args:
        m = re.match(r"Wiimote(?:New)?\.Wiimote[1-4]\.Source=(\d+)$", a)
        if m and int(m.group(1)) not in (WIIMOTE_NONE, WIIMOTE_EMULATED):
            die(f"override {a} would open a real Wii Remote")


DEFAULT_MIN_FREE_MB = 1500
DEFAULT_MIN_FREE_DISK_MB = 1000
DEFAULT_MIN_FREE_DISK_COPY_MB = 6000


def free_physical_memory_mb():
    """Available physical memory in MiB on Windows, or None on other systems."""
    if os.name != "nt":
        return None
    import ctypes

    class MEMORYSTATUSEX(ctypes.Structure):
        _fields_ = [
            ("dwLength", ctypes.c_ulong),
            ("dwMemoryLoad", ctypes.c_ulong),
            ("ullTotalPhys", ctypes.c_ulonglong),
            ("ullAvailPhys", ctypes.c_ulonglong),
            ("ullTotalPageFile", ctypes.c_ulonglong),
            ("ullAvailPageFile", ctypes.c_ulonglong),
            ("ullTotalVirtual", ctypes.c_ulonglong),
            ("ullAvailVirtual", ctypes.c_ulonglong),
            ("ullAvailExtendedVirtual", ctypes.c_ulonglong),
        ]

    stat = MEMORYSTATUSEX()
    stat.dwLength = ctypes.sizeof(MEMORYSTATUSEX)
    if not ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(stat)):
        return None
    return stat.ullAvailPhys // (1024 * 1024)


def free_disk_mb(path):
    """Free disk space in MiB on the drive holding `path`, or None if unavailable."""
    probe = os.path.abspath(path)
    while not os.path.exists(probe):
        parent = os.path.dirname(probe)
        if parent == probe:
            break
        probe = parent
    try:
        return shutil.disk_usage(probe).free // (1024 * 1024)
    except OSError:
        return None


def _env_nonneg_int(name, default):
    raw = env(name, required=False)
    if not raw:
        return default
    try:
        value = int(raw)
        if value < 0:
            raise ValueError
    except ValueError:
        die(f"{name} must be a non-negative integer (MB, or 0 to disable), not {raw!r}")
    return value


def check_launch_guards(sandbox_path, copy_card=False, *,
                        _mem_mb=free_physical_memory_mb, _disk_mb=free_disk_mb):
    """Refuse to start Dolphin when a hold file is present or free memory/disk
    is below the configured floor. A threshold of 0 disables that check."""
    hold = env("EMU_HOLD_FILE", required=False)
    if hold and os.path.exists(hold):
        die(f"emulators are on hold ({hold} exists)")

    min_mem = _env_nonneg_int("EMU_MIN_FREE_MB", DEFAULT_MIN_FREE_MB)
    if min_mem > 0:
        avail_mem = _mem_mb()
        if avail_mem is None:
            print("free physical memory: unknown, not checked")
        elif avail_mem < min_mem:
            die(f"free physical memory is {avail_mem} MB, below EMU_MIN_FREE_MB ({min_mem} MB); "
                f"set EMU_MIN_FREE_MB=0 to disable")

    default_disk = DEFAULT_MIN_FREE_DISK_COPY_MB if copy_card else DEFAULT_MIN_FREE_DISK_MB
    min_disk = _env_nonneg_int("EMU_MIN_FREE_DISK_MB", default_disk)
    if min_disk > 0:
        avail_disk = _disk_mb(sandbox_path)
        if avail_disk is None:
            print(f"free disk for {sandbox_path}: unknown, not checked")
        elif avail_disk < min_disk:
            die(f"free disk for {sandbox_path} is {avail_disk} MB, below EMU_MIN_FREE_DISK_MB ({min_disk} MB); "
                f"set EMU_MIN_FREE_DISK_MB=0 to disable")


def prepare(pads=None, writable_image=None, locked=False, wiimotes=None):
    """Check the settings, write the user folder's config, return (dolphin, user, base args).
    `pads` (ini values for ports 1-4) is given for unattended runs only; an
    interactive run leaves the controller setup alone and, without a writable
    image, always packs the SD folder. Every unattended run also writes
    Wiimote1..4 Source in WiimoteNew.ini (0 unless `wiimotes` asks for an
    emulated Remote), so a leftover emulated Remote cannot leak between suites.
    `writable_image` (storage_image for the storage tests, run_image for
    `make run`) boots that image with writes allowed (unless `locked`) and no
    folder sync at all."""
    dolphin = env_file("DOLPHIN")
    launcher = env_file("LAUNCHER_DOL")
    iso = env_file("BRAWL_ISO")
    user = os.path.abspath(env("DOLPHIN_USER"))
    sd_folder = os.path.abspath(env("SD_FOLDER")) if writable_image is None else env("SD_FOLDER", required=False)
    sd_image = os.path.abspath(env("SD_IMAGE"))
    if writable_image is None:
        plugins_dir(sd_folder)
    os.makedirs(os.path.dirname(sd_image), exist_ok=True)

    ini = {s: dict(kv) for s, kv in DOLPHIN_INI.items()}
    ini["Core"]["DefaultISO"] = iso
    ini.setdefault("General", {}).update({"WiiSDCardPath": sd_image,
                                          "WiiSDCardSyncFolder": os.path.abspath(sd_folder) if sd_folder else ""})
    if pads is not None:
        ini["Core"].update({f"SIDevice{i}": str(v) for i, v in enumerate(pads)})
    if writable_image is not None:
        # Even the sync folder is a decoy: an empty folder next to the copy.
        decoy = os.path.join(os.path.dirname(writable_image), STORAGE_DECOY_NAME)
        os.makedirs(decoy, exist_ok=True)
        if os.listdir(decoy):
            die(f"{decoy} should be empty")
        ini["General"].update({"WiiSDCardPath": writable_image, "WiiSDCardSyncFolder": decoy})
        ini["Core"].update(STORAGE_INI)
        if locked:
            ini["Core"]["WiiSDCardAllowWrites"] = "False"
    pack = writable_image is None and (pads is None or sd_needs_pack(sd_folder, sd_image))
    if writable_image is not None:
        print(f"card {writable_image}: {'locked' if locked else 'writes allowed'}, no folder sync")
    elif not pack:
        print(f"SD folder unchanged since {sd_image} was packed: using the image as it is")
    else:
        try:
            os.remove(stamp_path(sd_image))
        except OSError:
            pass
    if writable_image is None:
        ini["Core"]["WiiSDCardEnableFolderSync"] = "True" if pack else "False"
    update_ini(os.path.join(user, "Config", "Dolphin.ini"), ini)
    update_ini(os.path.join(user, "Config", "Logger.ini"), LOGGER_INI)
    update_ini(os.path.join(user, "Config", "GFX.ini"), GFX_INI)
    if pads is not None:
        w_sources = tuple(wiimotes) if wiimotes is not None else (WIIMOTE_NONE,) * 4
        if len(w_sources) != 4 or any(v not in (WIIMOTE_NONE, WIIMOTE_EMULATED) for v in w_sources):
            die(f"wiimotes must be four values in (0, 1), not {w_sources!r}")
        w_ini = {}
        for i, v in enumerate(w_sources, 1):
            sec = {"Source": str(v)}
            if v == WIIMOTE_EMULATED:
                sec["Options/Sideways Wiimote"] = "False"
            w_ini[f"Wiimote{i}"] = sec
        update_ini(os.path.join(user, "Config", "WiimoteNew.ini"), w_ini)

    seed = env("NAND_SEED", required=False)
    marker = os.path.join(user, "Wii", ".nand-seeded")
    if seed and not os.path.exists(marker):
        if not os.path.isdir(seed):
            die(f"NAND_SEED is not a folder: {seed}")
        shutil.copytree(seed, os.path.join(user, "Wii"), dirs_exist_ok=True)
        with open(marker, "w") as f:
            f.write("seeded\n")
        print(f"seeded the NAND from {seed}")

    return dolphin, user, ["-u", user, "-e", launcher]


def cmd_sd_folder():
    source = env_file("SD_SOURCE")
    folder = os.path.abspath(env("SD_FOLDER"))
    if os.path.isdir(folder) and os.listdir(folder):
        print(f"{folder} already exists; delete it to extract the card again")
        return 0
    return subprocess.call([sys.executable, os.path.join(HERE, "sd-extract.py"), source, "extract", folder])


def same_file(a, b):
    if not (os.path.isfile(a) and os.path.isfile(b)) or os.path.getsize(a) != os.path.getsize(b):
        return False
    with open(a, "rb") as fa, open(b, "rb") as fb:
        return fa.read() == fb.read()


def cmd_install(rel):
    if not os.path.isfile(rel):
        die(f"{rel} not found; run make")
    dest = os.path.join(plugins_dir(os.path.abspath(env("SD_FOLDER"))), os.path.basename(rel))
    if same_file(rel, dest):
        # Left untouched, so the SD image need not be packed again (see sd_needs_pack).
        print(f"{dest} already holds {rel}")
        return 0
    shutil.copyfile(rel, dest)
    print(f"installed {rel} -> {dest}")
    return 0


# Packing the SD folder (about 2 GB) into the image costs a lot of disk I/O.
# The game cannot write the card (WiiSDCardAllowWrites = False: the fork's
# SDIOSlot0 refuses writes and reports the card locked), so an image packed
# from the folder stays valid until something in the folder changes. After a
# run in which Dolphin logged a completed pack, a stamp is written next to the
# image; an unattended run skips the pack (folder sync off, the image used as
# it is) while the image and stamp are newer than everything in the folder.
PACKED_LINE = "Successfully packed folder"


def stamp_path(sd_image):
    return sd_image + ".packed"


def newest_mtime(folder):
    """Latest change in the folder, deleted files included: a deletion only
    changes its directory's time. Directories are stat'ed themselves, since
    on Windows the times a directory listing reports for its subdirectories
    come from the parent's index, which NTFS updates late (seen: a plugin
    file removed from pf/plugins left the listed time unchanged)."""
    newest = os.stat(folder).st_mtime
    stack = [folder]
    while stack:
        with os.scandir(stack.pop()) as it:
            for entry in it:
                if entry.is_dir(follow_symlinks=False):
                    newest = max(newest, os.stat(entry.path).st_mtime)
                    stack.append(entry.path)
                else:
                    newest = max(newest, entry.stat(follow_symlinks=False).st_mtime)
    return newest


def sd_needs_pack(sd_folder, sd_image):
    stamp = stamp_path(sd_image)
    try:
        with open(stamp, encoding="utf-8") as f:
            recorded = f.read().strip()
        image = os.stat(sd_image)
        stamped = os.stat(stamp).st_mtime
    except OSError:
        return True
    if recorded != f"{sd_folder}|{SD_IMAGE_BYTES}" or image.st_size != SD_IMAGE_BYTES or image.st_mtime > stamped:
        return True
    return newest_mtime(sd_folder) >= stamped


# The storage tests (storage_tests.py) need a card the game can write. They
# never use SD_FOLDER or SD_IMAGE for it: with writes allowed and folder sync
# on, Dolphin unpacks the whole image back into the folder when it stops
# (Common::SyncSDImageToSDFolder in the fork: the folder is renamed to a
# backup, the image unpacked into a new one, the backup deleted), which would
# rewrite the 2 GB SD folder after every run. Instead they boot a copy of
# SD_IMAGE (STORAGE_IMAGE_NAME next to it) with folder sync off, so Dolphin
# reads and writes that file only, packs nothing and unpacks nothing; the
# test reads the copy afterwards. Every other run keeps the locked card.
STORAGE_IMAGE_NAME = "sd-storage.raw"
CARD_IMAGE_NAME = "sd-card.raw"
STORAGE_DECOY_NAME = "sd-storage-nosync"
STORAGE_INI = {"WiiSDCardEnableFolderSync": "False", "WiiSDCardAllowWrites": "True"}
STORAGE_PLUGIN_PATH = "/Project+/pf/plugins/"
STORAGE_FOLDER = "/Project+/tm"


def remove_storage_image(path):
    """The storage tests' 2 GB card copy is made fresh for every run: removed
    after a run that passed (kept after a failure, to look at)."""
    if os.path.basename(path) in (STORAGE_IMAGE_NAME, CARD_IMAGE_NAME):
        try:
            os.remove(path)
        except OSError as e:
            print(f"test card copy not removed: {e}")


def remove_leftover_copies(sandbox_dir, exclude=()):
    """Before making a fresh 2 GB card copy, remove any leftover test copy
    (STORAGE_IMAGE_NAME or CARD_IMAGE_NAME only) from an earlier run in the
    same sandbox."""
    excluded = {os.path.normcase(os.path.abspath(p)) for p in exclude if p}
    for name in (STORAGE_IMAGE_NAME, CARD_IMAGE_NAME):
        path = os.path.join(sandbox_dir, name)
        if os.path.normcase(os.path.abspath(path)) in excluded:
            continue
        if not os.path.isfile(path):
            continue
        try:
            size_mb = os.path.getsize(path) // (1024 * 1024)
            os.remove(path)
            print(f"removed leftover test card copy: {path} ({size_mb} MB)")
        except OSError as e:
            print(f"leftover test card copy not removed: {path}: {e}")


def copy_card_image(source):
    """Copy an external SD card image `source` to the test storage area next
    to SD_IMAGE, without altering `source` or insisting on SD_IMAGE_BYTES.
    Returns the copy's path."""
    source = os.path.abspath(source)
    if not os.path.isfile(source):
        die(f"card image does not exist: {source}")
    sd_image = os.path.abspath(env("SD_IMAGE"))
    sd_folder = env("SD_FOLDER", required=False)
    os.makedirs(os.path.dirname(sd_image), exist_ok=True)
    dest = os.path.join(os.path.dirname(sd_image), CARD_IMAGE_NAME)
    in_folder = bool(sd_folder) and os.path.normcase(dest).startswith(os.path.normcase(os.path.abspath(sd_folder)) + os.sep)
    if os.path.normcase(dest) in (os.path.normcase(source), os.path.normcase(sd_image)) or in_folder:
        die(f"the card copy {dest} would overwrite the source, SD_IMAGE, or lie in SD_FOLDER")
    remove_leftover_copies(os.path.dirname(dest), exclude=(source, sd_image))
    check_launch_guards(os.path.dirname(dest), copy_card=True)
    t0 = time.monotonic()
    shutil.copyfile(source, dest)
    print(f"card test copy: {dest} ({os.path.getsize(dest)} bytes, copied in {time.monotonic() - t0:.1f}s)")
    return dest


def storage_image(rel):
    """A fresh writable card for the storage tests: SD_IMAGE copied next to
    itself, the plugin `rel` written into the copy and its tm/ folder emptied.
    Returns the copy's path."""
    import fatimage
    sd_image = os.path.abspath(env("SD_IMAGE"))
    sd_folder = os.path.abspath(env("SD_FOLDER"))
    dest = os.path.join(os.path.dirname(sd_image), STORAGE_IMAGE_NAME)
    in_folder = os.path.normcase(dest).startswith(os.path.normcase(sd_folder) + os.sep)
    if os.path.normcase(dest) == os.path.normcase(sd_image) or in_folder:
        die(f"the storage image {dest} would overwrite SD_IMAGE or lie in SD_FOLDER")
    if not os.path.isfile(sd_image) or os.path.getsize(sd_image) != SD_IMAGE_BYTES:
        die(f"{sd_image} is missing or not a packed card; run any other emulator test first")
    remove_leftover_copies(os.path.dirname(dest), exclude=(sd_image,))
    check_launch_guards(os.path.dirname(dest), copy_card=True)
    t0 = time.monotonic()
    shutil.copyfile(sd_image, dest)
    w = fatimage.FatWriter(dest)
    try:
        with open(rel, "rb") as f:
            plugin = f.read()
        w.put(STORAGE_PLUGIN_PATH + os.path.basename(rel), plugin)
        cleared = w.clear_folder(STORAGE_FOLDER)
    finally:
        w.close()
    if fatimage.read_file(dest, STORAGE_PLUGIN_PATH + os.path.basename(rel)) != plugin:
        die(f"{dest}: the plugin did not go in")
    print(f"storage test card: {dest} (copied in {time.monotonic() - t0:.1f}s, plugin written, "
          f"{cleared} old file(s) removed from {STORAGE_FOLDER})")
    return dest


def record_pack(log, sd_folder, sd_image):
    """After a run: stamp the image if this run packed it completely."""
    if PACKED_LINE in log:
        with open(stamp_path(sd_image), "w", encoding="utf-8") as f:
            f.write(f"{sd_folder}|{SD_IMAGE_BYTES}\n")


# `make run` plays on a card the game can write, as on a real setup, so the
# lab's settings and exported recordings are kept: a copy of SD_IMAGE
# (RUN_IMAGE_NAME next to it) booted with writes allowed and no folder sync,
# the way the storage tests boot theirs. The copy is made once and kept; each
# run only writes the current plugin into it. Delete the file to start again
# from SD_IMAGE (after the SD folder changed, for example).
RUN_IMAGE_NAME = "sd-run.raw"


def run_image(rel):
    """The writable card for `make run`, with the plugin `rel` in it, or None
    while SD_IMAGE has not been packed completely yet (the first run packs it)."""
    import fatimage
    sd_image = os.path.abspath(env("SD_IMAGE"))
    sd_folder = os.path.abspath(env("SD_FOLDER"))
    dest = os.path.join(os.path.dirname(sd_image), RUN_IMAGE_NAME)
    in_folder = os.path.normcase(dest).startswith(os.path.normcase(sd_folder) + os.sep)
    if os.path.normcase(dest) == os.path.normcase(sd_image) or in_folder:
        die(f"the run image {dest} would overwrite SD_IMAGE or lie in SD_FOLDER")
    if not os.path.isfile(dest):
        sized = os.path.isfile(sd_image) and os.path.getsize(sd_image) == SD_IMAGE_BYTES
        if not sized or not os.path.isfile(stamp_path(sd_image)):
            return None
        t0 = time.monotonic()
        shutil.copyfile(sd_image, dest)
        print(f"run card: {dest} made from {sd_image} in {time.monotonic() - t0:.1f}s")
    with open(rel, "rb") as f:
        plugin = f.read()
    path = STORAGE_PLUGIN_PATH + os.path.basename(rel)
    if fatimage.read_file(dest, path) != plugin:
        w = fatimage.FatWriter(dest)
        try:
            w.put(path, plugin)
        finally:
            w.close()
        if fatimage.read_file(dest, path) != plugin:
            die(f"{dest}: the plugin did not go in")
        print(f"run card: plugin written into {dest}")
    return dest


def cmd_run(rel=None):
    user = os.path.abspath(env("DOLPHIN_USER"))
    sd_image = os.path.abspath(env("SD_IMAGE"))
    will_copy = bool(rel) and not os.path.isfile(os.path.join(os.path.dirname(sd_image), RUN_IMAGE_NAME))
    check_launch_guards(user, copy_card=will_copy)
    image = run_image(rel) if rel else None
    if rel and image is None:
        print("first run: the SD folder is packed into the image now and the game cannot write the card; "
              "the next run uses a writable copy")
    dolphin, user, args = prepare(writable_image=image)
    cmd = [dolphin] + args
    print("launching:", subprocess.list2cmdline(cmd))
    log_path = os.path.join(user, "Logs", "dolphin.log")
    proc = subprocess.Popen(cmd, stdin=subprocess.DEVNULL)
    print(f"Dolphin pid {proc.pid}; log: {log_path}")
    try:
        code = proc.wait()
    except KeyboardInterrupt:
        stop(proc)
        return 130
    if image is None:
        record_pack(read_log(log_path), os.path.abspath(env("SD_FOLDER")), os.path.abspath(env("SD_IMAGE")))
    return code


def expected_lines():
    with open(os.path.join(REPO, "src", "shared", "version.h"), encoding="utf-8") as f:
        text = f.read()
    name = re.search(r'#define\s+TM_PLUGIN_NAME\s+"([^"]*)"', text).group(1)
    version = re.search(r'#define\s+TM_VERSION\s+"([^"]*)"', text).group(1)
    return f"[{name}] v{version} loaded", f"[Syringe] Loaded plugin ({name}, v{version})"


def stop(proc):
    """Stop the Dolphin process we started (and only it)."""
    if proc.poll() is None:
        proc.terminate()
        try:
            proc.wait(timeout=15)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait(timeout=15)


def stop_via_harness(harness, proc):
    """Ask Dolphin to quit (flushes its log), then make sure it is gone."""
    if proc.poll() is None and harness.sock is not None:
        try:
            harness.call("quit")
            proc.wait(timeout=15)
        except (OSError, HarnessError, ValueError, subprocess.TimeoutExpired):
            pass
    harness.close()
    stop(proc)


def speed_args(suite_speed):
    """The -C override for Dolphin's speed limit: EMU_SPEED if set, else the
    test's own `suite_speed`, else 0. 1 is real time (60 game frames a
    second), 2 twice that, 0 no limit, which is the default: every suite
    passes without the limit, and with it each frame costs a sixtieth of a
    second whatever the test does. Frame waits, pad scripts and frame
    advances are counted in game frames, so the limit only decides how long
    a frame takes on the host; a test that sleeps, takes a screenshot of a
    running game, or waits a fixed number of frames for files to load (file
    reads take host time) lets more frames pass meanwhile at a higher
    speed, and must wait on a condition instead."""
    value = env("EMU_SPEED", required=False) or suite_speed or "0"
    try:
        if float(value) < 0:
            raise ValueError
    except ValueError:
        die(f"EMU_SPEED must be a number, 0 (no limit) or more, not {value!r}")
    return ["-C", f"Dolphin.Core.EmulationSpeed={float(value):g}"]


def headless_backend():
    """Default video backend for suites that never assert on screenshots:
    Null unless VIDEO_BACKEND is set (set VIDEO_BACKEND=D3D to capture
    attachment or failure screenshots)."""
    return env("VIDEO_BACKEND", required=False) or "Null"


# ---- per-build Training CSS savestate cache ----------------------------------
# Cold-booting Launcher -> Gecko -> Brawl -> Training CSS takes several seconds
# on every suite. For standard read-only runs with TrainingMode.rel installed,
# we cache a Dolphin savestate + boot log sidecar right when Training CSS and
# the drill list are ready (drill_list="keep"), inside <DOLPHIN_USER>/StateSaves
# so concurrent sandboxes never race. Older keys are pruned automatically.
CSS_CACHE_PREFIX = "tm-css-"
CSS_CACHE_VERSION = 1
CSS_CACHE_MAX_PER_SANDBOX = 2


def css_cache_enabled():
    return env("EMU_CSS_STATE", required=False) != "0"


def file_sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while True:
            chunk = f.read(1 << 16)
            if not chunk:
                break
            h.update(chunk)
    return h.hexdigest()


def plugins_fingerprint(sd_folder):
    """(rel_sha256, [(lower_name, size, sha256), ...]) for pf/plugins, or
    (None, []) if TrainingMode.rel is absent."""
    pdir = plugins_dir(sd_folder)
    entries = []
    rel_sha = None
    for name in sorted(os.listdir(pdir), key=str.lower):
        full = os.path.join(pdir, name)
        if not os.path.isfile(full):
            continue
        digest = file_sha256(full)
        size = os.path.getsize(full)
        low = name.lower()
        entries.append([low, size, digest])
        if low == "trainingmode.rel":
            rel_sha = digest
    return rel_sha, entries


def _norm_extra_args(extra_args):
    """Normalize launch args for cache keying, ignoring EmulationSpeed."""
    out = []
    it = iter(extra_args)
    for a in it:
        if a == "-C":
            val = next(it, "")
            if not val.startswith("Dolphin.Core.EmulationSpeed="):
                out.append(["-C", val])
        elif not a.startswith("Dolphin.Core.EmulationSpeed="):
            out.append(a)
    return out


def _file_stamp(path):
    if not path:
        return None
    st = os.stat(path)
    return [st.st_size, st.st_mtime_ns]


def css_cache_key(user, sd_folder, sd_image, backend, extra_args, wiimotes=None, *,
                  dolphin=None, launcher=None, iso=None):
    """Return (short_key, signature_dict) for the Training CSS savestate cache,
    or (None, None) when caching is not applicable (e.g. plugin absent or SD
    image missing)."""
    try:
        rel_sha, plugins = plugins_fingerprint(sd_folder)
        if not rel_sha or not os.path.isfile(sd_image):
            return None, None
        sd_st = os.stat(sd_image)
        sig = {
            "v": CSS_CACHE_VERSION,
            "rel_sha256": rel_sha,
            "plugins": plugins,
            "sd_folder": os.path.normcase(os.path.abspath(sd_folder)),
            "sd_image_size": sd_st.st_size,
            "sd_image_mtime_ns": sd_st.st_mtime_ns,
            "backend": backend,
            "cpu_thread": not any(a == "Dolphin.Core.CPUThread=False" for a in extra_args),
            "pads": list(pad_ports(extra_args)),
            "wiimotes": list(wiimotes) if wiimotes is not None else [0, 0, 0, 0],
            "extra_args": _norm_extra_args(extra_args),
            "aspect": read_ini_value(os.path.join(user, "Config", "GFX.ini"), "Settings", "AspectRatio") or "",
            "dolphin": _file_stamp(dolphin),
            "launcher": _file_stamp(launcher),
            "iso": _file_stamp(iso),
        }
    except OSError:
        return None, None
    raw = json.dumps(sig, sort_keys=True, separators=(",", ":")).encode("utf-8")
    return hashlib.sha256(raw).hexdigest()[:16], sig


def _remove_quiet(path):
    try:
        os.remove(path)
    except OSError:
        pass


def prune_css_cache(states_dir, keep_key, keep_rel_sha256, max_entries=CSS_CACHE_MAX_PER_SANDBOX):
    """Remove stale or orphaned tm-css-* files in `states_dir`, keeping at most
    `max_entries` keys for `keep_rel_sha256` (always including `keep_key`)."""
    if not os.path.isdir(states_dir):
        return 0
    savs, metas = {}, {}
    removed = 0
    for name in os.listdir(states_dir):
        if not name.startswith(CSS_CACHE_PREFIX):
            continue
        full = os.path.join(states_dir, name)
        if name.endswith(".tmp"):
            _remove_quiet(full)
            removed += 1
        elif name.endswith(".sav"):
            savs[name[len(CSS_CACHE_PREFIX):-4]] = full
        elif name.endswith(".meta.json"):
            metas[name[len(CSS_CACHE_PREFIX):-10]] = full
    valid = []
    for key in set(savs) | set(metas):
        sav, meta_path = savs.get(key), metas.get(key)
        if not sav or not meta_path:
            if sav:
                _remove_quiet(sav)
                removed += 1
            if meta_path:
                _remove_quiet(meta_path)
                removed += 1
            continue
        if key == keep_key:
            continue
        rel_match = False
        try:
            with open(meta_path, encoding="utf-8") as f:
                meta = json.load(f)
            sig = meta.get("signature") or {}
            rel_match = (meta.get("version") == CSS_CACHE_VERSION
                         and sig.get("rel_sha256") == keep_rel_sha256)
            mtime = os.path.getmtime(meta_path)
        except (OSError, ValueError, TypeError):
            mtime = 0.0
        if not rel_match:
            _remove_quiet(sav)
            _remove_quiet(meta_path)
            removed += 2
        else:
            valid.append((mtime, key, sav, meta_path))
    keep_other = max(0, max_entries - (1 if keep_key in savs and keep_key in metas else 0))
    valid.sort(reverse=True)
    for _, _, sav, meta_path in valid[keep_other:]:
        _remove_quiet(sav)
        _remove_quiet(meta_path)
        removed += 2
    return removed


def splice_boot_log(log_path, prefix, post_load_offset):
    """Rewrite Dolphin's finished log file so E.read_log(log_path) sees the
    cached boot log followed by every line written after load_state."""
    prefix_bytes = prefix.encode("utf-8", "replace")
    prev = rotated_log_path(log_path)
    target = prev if os.path.isfile(prev) else log_path
    try:
        with open(target, "rb") as f:
            f.seek(post_load_offset)
            tail = f.read()
        with open(target, "wb") as f:
            f.write(prefix_bytes)
            f.write(tail)
    except OSError:
        pass


class CssCache:
    """Per-sandbox Training CSS savestate cache attached to an Emu."""

    def __init__(self, user, sd_folder, sd_image, backend, extra_args, wiimotes=None, *,
                 dolphin=None, launcher=None, iso=None, packed_this_run=False):
        self.user = user
        self.sd_folder = sd_folder
        self.sd_image = sd_image
        self.backend = backend
        self.extra_args = tuple(extra_args)
        self.wiimotes = wiimotes
        self.dolphin = dolphin
        self.launcher = launcher
        self.iso = iso
        self.packed_this_run = packed_this_run
        self.states_dir = os.path.join(user, "StateSaves")
        self._resolved = None

    def resolve(self):
        if self._resolved is None:
            self._resolved = css_cache_key(
                self.user, self.sd_folder, self.sd_image, self.backend, self.extra_args,
                self.wiimotes, dolphin=self.dolphin, launcher=self.launcher, iso=self.iso)
        return self._resolved

    def paths(self, key):
        return (os.path.join(self.states_dir, f"{CSS_CACHE_PREFIX}{key}.sav"),
                os.path.join(self.states_dir, f"{CSS_CACHE_PREFIX}{key}.meta.json"))

    def can_load(self):
        if self.packed_this_run:
            return False
        key, sig = self.resolve()
        if not key:
            return False
        sav, meta_path = self.paths(key)
        return os.path.isfile(sav) and os.path.isfile(meta_path)

    def invalidate(self, key=None):
        if key is None:
            key, _ = self.resolve()
        if not key:
            return
        sav, meta_path = self.paths(key)
        _remove_quiet(sav)
        _remove_quiet(sav + ".tmp")
        _remove_quiet(meta_path)
        _remove_quiet(meta_path + ".tmp")

    def load_meta(self):
        key, sig = self.resolve()
        if not key:
            return None, None, None
        sav, meta_path = self.paths(key)
        if not (os.path.isfile(sav) and os.path.isfile(meta_path)):
            return None, None, None
        try:
            with open(meta_path, encoding="utf-8") as f:
                meta = json.load(f)
        except (OSError, ValueError):
            self.invalidate(key)
            return None, None, None
        if (meta.get("version") != CSS_CACHE_VERSION or meta.get("key") != key
                or meta.get("signature") != sig or not isinstance(meta.get("boot_log"), str)):
            self.invalidate(key)
            return None, None, None
        return key, sav, meta

    def touch(self, key):
        _, meta_path = self.paths(key)
        try:
            os.utime(meta_path, None)
        except OSError:
            pass

    def save(self, emu, boot_log):
        key, sig = self.resolve()
        if not key:
            return False
        os.makedirs(self.states_dir, exist_ok=True)
        sav, meta_path = self.paths(key)
        sav_tmp = sav + ".tmp"
        _remove_quiet(sav_tmp)
        emu.h.call("pause")
        try:
            emu.h.call("frame_advance", n=1)
            emu.h.call("save_state", path=sav_tmp)
        except Exception:
            _remove_quiet(sav_tmp)
            raise
        finally:
            emu.h.call("resume")
        os.replace(sav_tmp, sav)
        meta = {"version": CSS_CACHE_VERSION, "key": key, "signature": sig, "boot_log": boot_log}
        tmp = meta_path + ".tmp"
        with open(tmp, "w", encoding="utf-8") as f:
            json.dump(meta, f)
        os.replace(tmp, meta_path)
        prune_css_cache(self.states_dir, keep_key=key, keep_rel_sha256=sig["rel_sha256"])
        size_mb = os.path.getsize(sav) / 1e6
        print(f"css cache: saved ({key[:8]}, {self.backend}, {size_mb:.1f} MB)")
        return True


@contextlib.contextmanager
def harness_run(backend, extra_args=(), writable_image=None, locked=False, speed=None, allow_null=False,
                wiimotes=None, css_cache=True):
    """Boot Project+ unattended with the harness on and yield (proc, Emu).
    The Dolphin process started here is stopped on the way out, whatever happens.
    With `writable_image` (storage_image()), the game may write that card
    (`locked`: the same card, write-protected as every other test's).
    `speed` is the test's own speed limit (see speed_args).
    `allow_null` permits the Null backend for suites that do not assert on
    screenshots. `wiimotes` (four values in (0, 1)) enables emulated Wii
    Remotes in WiimoteNew.ini; omitted, all four slots are set to 0.
    `css_cache` permits restoring/saving the per-build Training CSS savestate
    cache (automatically disabled for writable cards, emulated Wiimotes, or
    when EMU_CSS_STATE=0)."""
    if backend == "Null" and not allow_null:
        die("VIDEO_BACKEND must be a real backend: Null renders nothing to capture")
    check_launch_guards(os.path.abspath(env("DOLPHIN_USER")))
    port = int(env("HARNESS_PORT", required=False) or free_port())
    raw_extra = tuple(extra_args)
    sd_packed = (writable_image is None
                 and sd_needs_pack(os.path.abspath(env("SD_FOLDER")), os.path.abspath(env("SD_IMAGE"))))
    dolphin, user, args = prepare(pad_ports(extra_args), writable_image, locked, wiimotes=wiimotes)
    shots = os.path.abspath(env("SCREENSHOT_DIR", required=False) or os.path.join(user, "ScreenShots"))
    log_path = os.path.join(user, "Logs", "dolphin.log")
    for old in (log_path, rotated_log_path(log_path)):
        if os.path.exists(old):
            os.remove(old)
    extra_args = list(extra_args) + speed_args(speed)
    cmd = [dolphin] + args + QUIET_ARGS + extra_args + ["-v", backend, "--harness-port", str(port)]
    check_pads(os.path.join(user, "Config", "Dolphin.ini"), cmd)
    print("launching:", subprocess.list2cmdline(cmd))
    proc = subprocess.Popen(cmd, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL, **low_priority())
    print(f"Dolphin pid {proc.pid}")
    harness = Harness(port)
    emu = None
    try:
        if not harness.connect(proc, 120):
            raise HarnessError("could not connect to the harness (is this the Brawl Online Dolphin build?)")
        emu = Emu(harness, log_path, shots, headless=(backend == "Null"))
        if css_cache and css_cache_enabled() and writable_image is None and not (wiimotes and any(wiimotes)):
            emu.css_cache = CssCache(
                user, os.path.abspath(env("SD_FOLDER")), os.path.abspath(env("SD_IMAGE")),
                backend, raw_extra, wiimotes=wiimotes,
                dolphin=dolphin, launcher=env_file("LAUNCHER_DOL"), iso=env_file("BRAWL_ISO"),
                packed_this_run=sd_packed)
        # The SD repack can take minutes on a busy machine; wait_running fails
        # earlier if nothing progresses.
        emu.wait_running(proc, 1800)
        yield proc, emu
        if emu.audit:
            print(f"harness audit: {emu.audited[0]} batches, {emu.audited[1]} reads, each equal to the same read "
                  f"sent on its own")
    finally:
        stop_via_harness(harness, proc)
        if writable_image is None:
            record_pack(read_log(log_path), os.path.abspath(env("SD_FOLDER")), os.path.abspath(env("SD_IMAGE")))
        else:
            # Back to the locked card for whatever runs next in this user folder.
            sd_folder = env("SD_FOLDER", required=False)
            general = {"WiiSDCardPath": os.path.abspath(env("SD_IMAGE"))}
            if sd_folder:
                general["WiiSDCardSyncFolder"] = os.path.abspath(sd_folder)
            update_ini(os.path.join(user, "Config", "Dolphin.ini"),
                       {"General": general, "Core": {"WiiSDCardAllowWrites": "False"}})
        if emu is not None and emu._boot_log_prefix is not None:
            splice_boot_log(log_path, emu._boot_log_prefix, emu._post_load_offset)


def read_log(path):
    """The run's log, with the part Dolphin moved aside when the log filled
    (harness_run removes an earlier run's; only the last one moved is kept)."""
    text = ""
    for part in (rotated_log_path(path), path):
        try:
            with open(part, "rb") as f:
                text += f.read().decode("utf-8", "replace")
        except OSError:
            pass
    return text


def sd_sync_seconds(log):
    """Seconds Dolphin took to pack SD_FOLDER into SD_IMAGE, from its own log lines."""
    start = end = None
    for line in log.splitlines():
        if start is None and "Starting SD card conversion from folder" in line:
            start = line
        elif start is not None and "Successfully packed folder" in line:
            end = line
            break
    if not end:
        return None
    t = [re.match(r"(\d+):(\d+):(\d+)", s) for s in (start, end)]
    if not all(t):
        return None
    a, b = ((int(m[1]) * 60 + int(m[2])) * 1000 + int(m[3]) for m in t)
    return ((b - a) % 3600000) / 1000.0


def check_image(sd_image, rel):
    """True if the generated image holds the same bytes as `rel` in pf/plugins."""
    import importlib.util
    sys.dont_write_bytecode = True  # no __pycache__ in the repo
    spec = importlib.util.spec_from_file_location("sd_extract", os.path.join(HERE, "sd-extract.py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    fat = mod.Fat(sd_image)
    try:
        data = fat.get("/Project+/pf/plugins/" + os.path.basename(rel))
    except FileNotFoundError:
        return False
    finally:
        fat.close()
    with open(rel, "rb") as f:
        return data == f.read()


# test-load: lines that must appear (regexes, {name} is the plugin name), and
# lines that must not appear by the time the last of them has.
LOAD_LINES = [
    ("hook render", r"\[{name}\] hook render: installed at 8001792c"),
    ("hook scene exit", r"\[{name}\] hook scene exit: installed at 8002d288"),
    ("hook scene start", r"\[{name}\] hook scene start: installed at 8002d248"),
    ("hook pads system", r"\[{name}\] hook pads system: installed at 8002a274"),
    ("hook pads game", r"\[{name}\] hook pads game: installed at 8002a7a8"),
    ("hook pads fighter", r"\[{name}\] hook pads fighter: installed at 8004aa70"),
    ("hook fighter input", r"\[{name}\] hook fighter input: installed at 80764e9c"),
    ("plugin file checked", r"\[{name}\] modules: \S*pf/plugins/{name}\.rel: ok;"),
    ("match module checked", r"\[{name}\] module MTCH: id \d+, .*: ok"),
    ("render call 120", r"\[{name}\] render hook: call 120,"),
]
LOAD_FORBIDDEN = [
    ("unknown stub layout", r"\[{name}\] hook \w+( \w+)?: stub check: unrecognised"),
    ("module loaded at boot", r"\[{name}\] module \w+: loaded"),
    ("module attached at boot", r"\[{name}\] main: attached"),
]


# test-load: TEST_TIMEOUT is how long the log may stay unchanged (the SD
# repack at boot logs nothing until it ends, and on a busy machine it is
# slow), this the overall limit.
LOAD_CEILING_S = 1800


def cmd_test_load(rel):
    ours, syringe = expected_lines()
    name = re.escape(ours[1:ours.index("]")])
    wanted = [(label, re.compile(rx.format(name=name))) for label, rx in LOAD_LINES]
    forbidden = [(label, re.compile(rx.format(name=name))) for label, rx in LOAD_FORBIDDEN]
    timeout = float(env("TEST_TIMEOUT", required=False) or 600)
    check_launch_guards(os.path.abspath(env("DOLPHIN_USER")))
    dolphin, user, args = prepare(pad_ports(()))
    log_path = os.path.join(user, "Logs", "dolphin.log")
    for old in (log_path, rotated_log_path(log_path)):
        if os.path.exists(old):
            os.remove(old)
    sd_image = os.path.abspath(env("SD_IMAGE"))
    cmd = [dolphin] + args + TEST_ARGS
    check_pads(os.path.join(user, "Config", "Dolphin.ini"), cmd)
    print("launching:", subprocess.list2cmdline(cmd))
    t0 = time.monotonic()
    proc = subprocess.Popen(cmd, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL, **low_priority())
    print(f"Dolphin pid {proc.pid}; waiting for the plugin to load (gives up after {timeout:.0f}s "
          f"without a new log line, {LOAD_CEILING_S}s in all)")
    why, log = "timed out", ""
    last, size = time.monotonic(), -1
    try:
        while time.monotonic() - t0 < LOAD_CEILING_S and time.monotonic() - last < timeout:
            log = read_log(log_path)
            if len(log) != size:
                size, last = len(log), time.monotonic()
            if ours in log and syringe in log and all(rx.search(log) for _, rx in wanted):
                why = "" if log.index(ours) < log.index(syringe) else "the load lines are in the wrong order"
                break
            if proc.poll() is not None:
                why = f"Dolphin exited with code {proc.returncode}"
                break
            time.sleep(0.5)
    finally:
        stop(proc)
    elapsed = time.monotonic() - t0
    log = read_log(log_path)
    record_pack(log, os.path.abspath(env("SD_FOLDER")), sd_image)

    synced = check_image(sd_image, rel) if os.path.exists(sd_image) else False
    if not synced:
        why = why or "the SD image does not hold the installed plugin"
    bad = [label for label, rx in forbidden if rx.search(log)]
    if bad and not why:
        why = "found: " + ", ".join(bad)

    print()
    for line in log.splitlines():
        if "[Syringe]" in line or f"[{ours[1:ours.index(']')]}]" in line or "packed folder" in line:
            print("  " + line)
        if "render hook: call 120" in line:
            break
    secs = sd_sync_seconds(log)
    print()
    for label, rx in wanted:
        print(f"{label:>22}: {'yes' if rx.search(log) else 'NO'}")
    for label, rx in forbidden:
        print(f"{label:>22}: {'FOUND' if rx.search(log) else 'no'}")
    print(f"SD image holds the installed {os.path.basename(rel)}: {'yes' if synced else 'NO'}")
    print(f"SD folder -> image pack: {secs:.1f}s" if secs is not None else "SD folder -> image pack: skipped (folder unchanged)")
    print(f"time: {elapsed:.1f}s")
    if not why:
        print("test-load: PASS")
        return 0
    print(f"test-load: FAIL ({why})")
    print(f"--- last 40 lines of {log_path} ---")
    for line in log.splitlines()[-40:]:
        print(line)
    return 1


def cmd_self_check():
    """Host-only self-check of the launch guards and leftover copy cleanup."""
    import io
    import tempfile

    guard_vars = ("EMU_HOLD_FILE", "EMU_MIN_FREE_MB", "EMU_MIN_FREE_DISK_MB")
    saved = {k: os.environ.get(k) for k in guard_vars}

    def set_env(**overrides):
        for k in guard_vars:
            os.environ.pop(k, None)
        for k, v in overrides.items():
            if v is not None:
                os.environ[k] = str(v)

    def run_guard(sandbox, copy_card=False, mem=8192, disk=20000):
        out, err = io.StringIO(), io.StringIO()
        code = 0
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            try:
                check_launch_guards(sandbox, copy_card=copy_card,
                                    _mem_mb=lambda: mem, _disk_mb=lambda _p: disk)
            except SystemExit as e:
                code = e.code
        return code, out.getvalue(), err.getvalue()

    passed = 0
    failed = []

    def check(name, cond, detail=""):
        nonlocal passed
        if cond:
            passed += 1
            print(f"ok   {name}")
        else:
            failed.append(f"{name}: {detail}")
            print(f"FAIL {name}: {detail}")

    try:
        with tempfile.TemporaryDirectory() as tmp:
            hold = os.path.join(tmp, "hold")
            # Hold file absent -> pass; present -> die.
            set_env(EMU_HOLD_FILE=hold)
            c, o, e = run_guard(tmp)
            check("hold_file_absent", c == 0, f"code={c} err={e!r}")

            with open(hold, "w", encoding="utf-8") as f:
                f.write("hold\n")
            c, o, e = run_guard(tmp)
            check("hold_file_present", c == 2 and "on hold" in e, f"code={c} err={e!r}")
            os.remove(hold)

            # Memory below default (1500), at default, custom, switch-off (0), unknown.
            set_env()
            c, o, e = run_guard(tmp, mem=1499)
            check("memory_below_default", c == 2 and "EMU_MIN_FREE_MB" in e, f"code={c} err={e!r}")
            c, o, e = run_guard(tmp, mem=1500)
            check("memory_at_default", c == 0, f"code={c} err={e!r}")

            set_env(EMU_MIN_FREE_MB="2048")
            c, o, e = run_guard(tmp, mem=2047)
            check("memory_below_custom", c == 2 and "2048" in e, f"code={c} err={e!r}")
            c, o, e = run_guard(tmp, mem=2048)
            check("memory_at_custom", c == 0, f"code={c} err={e!r}")

            set_env(EMU_MIN_FREE_MB="0")
            c, o, e = run_guard(tmp, mem=1)
            check("memory_switch_off", c == 0, f"code={c} err={e!r}")

            set_env()
            c, o, e = run_guard(tmp, mem=None)
            check("memory_unknown", c == 0 and "unknown, not checked" in o, f"code={c} out={o!r} err={e!r}")

            # Disk below default (1000 normal, 6000 copy_card), at default, custom, switch-off (0), unknown.
            set_env()
            c, o, e = run_guard(tmp, copy_card=False, disk=999)
            check("disk_below_default", c == 2 and "EMU_MIN_FREE_DISK_MB" in e, f"code={c} err={e!r}")
            c, o, e = run_guard(tmp, copy_card=False, disk=1000)
            check("disk_at_default", c == 0, f"code={c} err={e!r}")

            c, o, e = run_guard(tmp, copy_card=True, disk=5999)
            check("disk_below_copy_default", c == 2 and "6000" in e, f"code={c} err={e!r}")
            c, o, e = run_guard(tmp, copy_card=True, disk=6000)
            check("disk_at_copy_default", c == 0, f"code={c} err={e!r}")

            set_env(EMU_MIN_FREE_DISK_MB="2500")
            c, o, e = run_guard(tmp, copy_card=True, disk=2499)
            check("disk_below_custom", c == 2 and "2500" in e, f"code={c} err={e!r}")
            c, o, e = run_guard(tmp, copy_card=True, disk=2500)
            check("disk_at_custom", c == 0, f"code={c} err={e!r}")

            set_env(EMU_MIN_FREE_DISK_MB="0")
            c, o, e = run_guard(tmp, copy_card=False, disk=1)
            check("disk_switch_off_normal", c == 0, f"code={c} err={e!r}")
            c, o, e = run_guard(tmp, copy_card=True, disk=1)
            check("disk_switch_off_copy", c == 0, f"code={c} err={e!r}")

            set_env()
            c, o, e = run_guard(tmp, disk=None)
            check("disk_unknown", c == 0 and "unknown, not checked" in o, f"code={c} out={o!r} err={e!r}")

            # Invalid threshold values.
            set_env(EMU_MIN_FREE_MB="-1")
            c, o, e = run_guard(tmp)
            check("invalid_min_mem", c == 2 and "EMU_MIN_FREE_MB" in e, f"code={c} err={e!r}")
            set_env(EMU_MIN_FREE_DISK_MB="abc")
            c, o, e = run_guard(tmp)
            check("invalid_min_disk", c == 2 and "EMU_MIN_FREE_DISK_MB" in e, f"code={c} err={e!r}")

            # Leftover copy cleanup: removes only STORAGE_IMAGE_NAME / CARD_IMAGE_NAME.
            for fn in (STORAGE_IMAGE_NAME, CARD_IMAGE_NAME, RUN_IMAGE_NAME, "sd.raw", "keep.txt"):
                with open(os.path.join(tmp, fn), "wb") as f:
                    f.write(b"x")
            buf = io.StringIO()
            with contextlib.redirect_stdout(buf):
                remove_leftover_copies(tmp, exclude=(os.path.join(tmp, "sd.raw"),))
            out_txt = buf.getvalue()
            check("leftover_copies_removed",
                  not os.path.exists(os.path.join(tmp, STORAGE_IMAGE_NAME))
                  and not os.path.exists(os.path.join(tmp, CARD_IMAGE_NAME))
                  and os.path.exists(os.path.join(tmp, RUN_IMAGE_NAME))
                  and os.path.exists(os.path.join(tmp, "sd.raw"))
                  and os.path.exists(os.path.join(tmp, "keep.txt"))
                  and "removed leftover test card copy" in out_txt,
                  f"out={out_txt!r}")

            # Real host probes.
            mem_real = free_physical_memory_mb()
            check("real_free_memory_probe",
                  (isinstance(mem_real, int) and mem_real > 0) if os.name == "nt" else (mem_real is None),
                  f"mem_real={mem_real!r}")
            disk_real = free_disk_mb(os.path.join(tmp, "nonexistent", "child"))
            check("real_free_disk_probe",
                  isinstance(disk_real, int) and disk_real >= 0,
                  f"disk_real={disk_real!r}")

            # Wiimote safety: prepare() resets Wiimote1..4 Source = 0 on unattended
            # runs that do not request an emulated Remote, and check_pads() refuses
            # Source = 2 (real Bluetooth) in WiimoteNew.ini or -C overrides.
            env_keys = ("DOLPHIN", "LAUNCHER_DOL", "BRAWL_ISO", "DOLPHIN_USER", "SD_FOLDER", "SD_IMAGE", "NAND_SEED")
            saved_emu = {k: os.environ.get(k) for k in env_keys}
            try:
                fake_bin = os.path.join(tmp, "fake.bin")
                with open(fake_bin, "wb") as f:
                    f.write(b"x")
                user_dir = os.path.join(tmp, "user")
                sd_dir = os.path.join(tmp, "sd")
                os.makedirs(os.path.join(sd_dir, "Project+", "pf", "plugins"), exist_ok=True)
                os.environ["DOLPHIN"] = fake_bin
                os.environ["LAUNCHER_DOL"] = fake_bin
                os.environ["BRAWL_ISO"] = fake_bin
                os.environ["DOLPHIN_USER"] = user_dir
                os.environ["SD_FOLDER"] = sd_dir
                os.environ["SD_IMAGE"] = os.path.join(tmp, "sd.raw")
                os.environ.pop("NAND_SEED", None)

                w_ini = os.path.join(user_dir, "Config", "WiimoteNew.ini")
                d_ini = os.path.join(user_dir, "Config", "Dolphin.ini")
                update_ini(w_ini, {"Wiimote1": {"Source": "1"}, "Wiimote2": {"Source": "2"}})
                with contextlib.redirect_stdout(io.StringIO()):
                    prepare(pad_ports(()))
                got_reset = [read_ini_value(w_ini, f"Wiimote{i}", "Source") for i in range(1, 5)]
                check("prepare_resets_wiimotes", got_reset == ["0", "0", "0", "0"], f"got={got_reset!r}")

                def run_check_pads(extra_cmd=()):
                    err = io.StringIO()
                    code = 0
                    with contextlib.redirect_stderr(err):
                        try:
                            check_pads(d_ini, list(extra_cmd))
                        except SystemExit as e:
                            code = e.code
                    return code, err.getvalue()

                c, e = run_check_pads()
                check("check_pads_accepts_disabled_wiimotes", c == 0, f"code={c} err={e!r}")

                try:
                    with contextlib.redirect_stdout(io.StringIO()):
                        prepare(pad_ports(()), wiimotes=(1, 0, 0, 0))
                    got_emu = [read_ini_value(w_ini, f"Wiimote{i}", "Source") for i in range(1, 5)]
                    check("prepare_enables_requested_wiimote", got_emu == ["1", "0", "0", "0"], f"got={got_emu!r}")
                except TypeError as exc:
                    check("prepare_enables_requested_wiimote", False, str(exc))

                c, e = run_check_pads()
                check("check_pads_accepts_emulated_wiimote", c == 0, f"code={c} err={e!r}")

                update_ini(w_ini, {"Wiimote1": {"Source": "2"}})
                c, e = run_check_pads()
                check("check_pads_refuses_real_wiimote_ini", c == 2 and "Wiimote1" in e, f"code={c} err={e!r}")

                update_ini(w_ini, {"Wiimote1": {"Source": "0"}})
                c, e = run_check_pads(["-C", "WiimoteNew.Wiimote1.Source=2"])
                check("check_pads_refuses_real_wiimote_override", c == 2 and "Wiimote" in e, f"code={c} err={e!r}")

                # CSS savestate cache key, invalidation, pruning, EMU_CSS_STATE=0,
                # boot-log splicing, and TCP_NODELAY.
                rel_file = os.path.join(sd_dir, "Project+", "pf", "plugins", "TrainingMode.rel")
                with open(rel_file, "wb") as f:
                    f.write(b"REL-V1")
                sd_img = os.path.join(tmp, "sd.raw")
                with open(sd_img, "wb") as f:
                    f.write(b"SD-IMAGE-DATA")
                gfx_ini = os.path.join(user_dir, "Config", "GFX.ini")

                k_base, sig_base = css_cache_key(
                    user_dir, sd_dir, sd_img, "Null", DETERMINISTIC_ARGS,
                    dolphin=fake_bin, launcher=fake_bin, iso=fake_bin)
                k_same, _ = css_cache_key(
                    user_dir, sd_dir, sd_img, "Null", DETERMINISTIC_ARGS + ["-C", "Dolphin.Core.EmulationSpeed=0"],
                    dolphin=fake_bin, launcher=fake_bin, iso=fake_bin)
                check("css_cache_key_deterministic_and_ignores_speed",
                      bool(k_base) and k_base == k_same, f"k_base={k_base!r} k_same={k_same!r}")

                k_d3d, _ = css_cache_key(
                    user_dir, sd_dir, sd_img, "D3D", DETERMINISTIC_ARGS,
                    dolphin=fake_bin, launcher=fake_bin, iso=fake_bin)
                check("css_cache_key_invalidates_on_backend",
                      bool(k_d3d) and k_d3d != k_base, f"k_base={k_base!r} k_d3d={k_d3d!r}")

                k_dual, _ = css_cache_key(
                    user_dir, sd_dir, sd_img, "Null", (),
                    dolphin=fake_bin, launcher=fake_bin, iso=fake_bin)
                k_4pad, _ = css_cache_key(
                    user_dir, sd_dir, sd_img, "Null", DETERMINISTIC_ARGS + FOUR_PADS_ARGS,
                    dolphin=fake_bin, launcher=fake_bin, iso=fake_bin)
                k_wii, _ = css_cache_key(
                    user_dir, sd_dir, sd_img, "Null", DETERMINISTIC_ARGS, wiimotes=(1, 0, 0, 0),
                    dolphin=fake_bin, launcher=fake_bin, iso=fake_bin)
                check("css_cache_key_invalidates_on_args_and_wiimotes",
                      len({k_base, k_dual, k_4pad, k_wii}) == 4,
                      f"keys={(k_base, k_dual, k_4pad, k_wii)!r}")

                update_ini(gfx_ini, {"Settings": {"AspectRatio": "2"}})
                k_aspect, _ = css_cache_key(
                    user_dir, sd_dir, sd_img, "Null", DETERMINISTIC_ARGS,
                    dolphin=fake_bin, launcher=fake_bin, iso=fake_bin)
                check("css_cache_key_invalidates_on_aspect",
                      bool(k_aspect) and k_aspect != k_base, f"k_base={k_base!r} k_aspect={k_aspect!r}")

                with open(rel_file, "wb") as f:
                    f.write(b"REL-V2-CHANGED")
                k_rel2, sig_rel2 = css_cache_key(
                    user_dir, sd_dir, sd_img, "Null", DETERMINISTIC_ARGS,
                    dolphin=fake_bin, launcher=fake_bin, iso=fake_bin)
                check("css_cache_key_invalidates_on_rel_change",
                      bool(k_rel2) and k_rel2 != k_aspect and sig_rel2["rel_sha256"] != sig_base["rel_sha256"],
                      f"k_aspect={k_aspect!r} k_rel2={k_rel2!r}")

                os.remove(rel_file)
                k_missing, _ = css_cache_key(
                    user_dir, sd_dir, sd_img, "Null", DETERMINISTIC_ARGS,
                    dolphin=fake_bin, launcher=fake_bin, iso=fake_bin)
                check("css_cache_key_none_when_rel_absent", k_missing is None, f"got={k_missing!r}")

                # EMU_CSS_STATE=0 bypass.
                saved_css_env = os.environ.get("EMU_CSS_STATE")
                try:
                    os.environ.pop("EMU_CSS_STATE", None)
                    en_default = css_cache_enabled()
                    os.environ["EMU_CSS_STATE"] = "0"
                    en_zero = css_cache_enabled()
                    os.environ["EMU_CSS_STATE"] = "1"
                    en_one = css_cache_enabled()
                    check("css_cache_env_bypass", en_default and (not en_zero) and en_one,
                          f"default={en_default} zero={en_zero} one={en_one}")
                finally:
                    if saved_css_env is None:
                        os.environ.pop("EMU_CSS_STATE", None)
                    else:
                        os.environ["EMU_CSS_STATE"] = saved_css_env

                # Pruning: remove old rel hash, orphan files, and excess keys past max_entries=2.
                sdir = os.path.join(user_dir, "StateSaves")
                os.makedirs(sdir, exist_ok=True)
                def make_entry(k, rel_h, mtime):
                    sav = os.path.join(sdir, f"{CSS_CACHE_PREFIX}{k}.sav")
                    meta = os.path.join(sdir, f"{CSS_CACHE_PREFIX}{k}.meta.json")
                    with open(sav, "wb") as f:
                        f.write(b"sav")
                    with open(meta, "w", encoding="utf-8") as f:
                        json.dump({"version": CSS_CACHE_VERSION, "key": k,
                                   "signature": {"rel_sha256": rel_h}, "boot_log": ""}, f)
                    os.utime(meta, (mtime, mtime))
                make_entry("oldrel", "hash_old", 100)
                make_entry("cur1", "hash_cur", 200)
                make_entry("cur2", "hash_cur", 300)
                make_entry("cur3", "hash_cur", 400)
                with open(os.path.join(sdir, f"{CSS_CACHE_PREFIX}orphan.sav"), "wb") as f:
                    f.write(b"x")
                with open(os.path.join(sdir, f"{CSS_CACHE_PREFIX}stale.meta.json.tmp"), "wb") as f:
                    f.write(b"x")
                with open(os.path.join(sdir, "unrelated.sav"), "wb") as f:
                    f.write(b"keep")
                prune_css_cache(sdir, keep_key="cur3", keep_rel_sha256="hash_cur", max_entries=2)
                remaining = sorted(os.listdir(sdir))
                want_rem = sorted([f"{CSS_CACHE_PREFIX}cur2.sav", f"{CSS_CACHE_PREFIX}cur2.meta.json",
                                   f"{CSS_CACHE_PREFIX}cur3.sav", f"{CSS_CACHE_PREFIX}cur3.meta.json",
                                   "unrelated.sav"])
                check("css_cache_prune_stale_orphans_and_cap", remaining == want_rem,
                      f"remaining={remaining!r} want={want_rem!r}")

                # CssCache.invalidate() and corrupt/mismatched meta recovery.
                with open(rel_file, "wb") as f:
                    f.write(b"REL-V3")
                cache_obj = CssCache(
                    user_dir, sd_dir, sd_img, "Null", DETERMINISTIC_ARGS,
                    dolphin=fake_bin, launcher=fake_bin, iso=fake_bin)
                ck, csig = cache_obj.resolve()
                csav, cmeta = cache_obj.paths(ck)
                for pth in (csav, csav + ".tmp", cmeta, cmeta + ".tmp"):
                    with open(pth, "wb") as f:
                        f.write(b"data")
                cache_obj.invalidate(ck)
                check("css_cache_invalidate_removes_pair_and_temps",
                      all(not os.path.exists(pth) for pth in (csav, csav + ".tmp", cmeta, cmeta + ".tmp"))
                      and os.path.exists(os.path.join(sdir, "unrelated.sav")),
                      f"sdir={sorted(os.listdir(sdir))!r}")
                with open(csav, "wb") as f:
                    f.write(b"sav")
                with open(cmeta, "w", encoding="utf-8") as f:
                    f.write("{corrupt json")
                r_bad = cache_obj.load_meta()
                bad_purged = (r_bad == (None, None, None) and not os.path.exists(csav) and not os.path.exists(cmeta))
                with open(csav, "wb") as f:
                    f.write(b"sav")
                with open(cmeta, "w", encoding="utf-8") as f:
                    json.dump({"version": CSS_CACHE_VERSION + 1, "key": ck, "signature": csig, "boot_log": ""}, f)
                r_ver = cache_obj.load_meta()
                ver_purged = (r_ver == (None, None, None) and not os.path.exists(csav) and not os.path.exists(cmeta))
                check("css_cache_corrupt_meta_purges_pair", bad_purged and ver_purged,
                      f"bad={r_bad!r} bad_purged={bad_purged} ver={r_ver!r} ver_purged={ver_purged}")

                # Boot log splicing keeps cached boot lines + post-load tail, dropping pre-load startup.
                test_log = os.path.join(tmp, "dolphin.log")
                with open(test_log, "wb") as f:
                    f.write(b"PRE-LOAD-LINE\nPOST-LOAD-LINE\n")
                splice_boot_log(test_log, "CACHED-BOOT\n", len(b"PRE-LOAD-LINE\n"))
                spliced = read_log(test_log)
                check("splice_boot_log_replaces_pre_load", spliced == "CACHED-BOOT\nPOST-LOAD-LINE\n",
                      f"got={spliced!r}")

                # TCP_NODELAY set on Harness.connect().
                import socket as _sock
                import threading as _thr
                srv = _sock.socket(_sock.AF_INET, _sock.SOCK_STREAM)
                srv.bind(("127.0.0.1", 0))
                srv.listen(1)
                srv_port = srv.getsockname()[1]
                accepted = []
                def _accept():
                    c_sock, _ = srv.accept()
                    accepted.append(c_sock)
                thr = _thr.Thread(target=_accept, daemon=True)
                thr.start()
                class _AliveProc:
                    def poll(self):
                        return None
                h_client = Harness(srv_port)
                ok_conn = h_client.connect(_AliveProc(), 5.0)
                nodelay = h_client.sock.getsockopt(_sock.IPPROTO_TCP, _sock.TCP_NODELAY) if ok_conn else 0
                h_client.close()
                thr.join(timeout=2.0)
                for s_acc in accepted:
                    s_acc.close()
                srv.close()
                check("harness_connect_sets_tcp_nodelay", ok_conn and nodelay != 0,
                      f"connected={ok_conn} nodelay={nodelay}")
            finally:
                for k, v in saved_emu.items():
                    if v is None:
                        os.environ.pop(k, None)
                    else:
                        os.environ[k] = v
    finally:
        for k, v in saved.items():
            if v is None:
                os.environ.pop(k, None)
            else:
                os.environ[k] = v

    print(f"emu.py self-check: {passed} passed, {len(failed)} failed")
    return 1 if failed else 0


def main():
    sys.stdout.reconfigure(line_buffering=True)
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    cmd, rest = sys.argv[1], sys.argv[2:]
    if cmd == "self-check" and not rest:
        return cmd_self_check()
    if cmd == "sd-folder" and not rest:
        return cmd_sd_folder()
    if cmd == "install" and len(rest) == 1:
        return cmd_install(rest[0])
    if cmd == "run" and len(rest) <= 1:
        return cmd_run(rest[0] if rest else None)
    if cmd == "test-load" and len(rest) == 1:
        return cmd_test_load(rest[0])
    import loader_tests
    if cmd in loader_tests.COMMANDS and rest:
        return loader_tests.COMMANDS[cmd](rest)
    if cmd == "test-lab" and rest:
        import lab_tests
        return lab_tests.cmd_test_lab(rest)
    if cmd == "test-displays" and rest:
        import display_tests
        return display_tests.cmd_test_displays(rest)
    if cmd == "test-osds" and rest:
        import osd_tests
        return osd_tests.cmd_test_osds(rest)
    if cmd == "test-drills" and rest:
        import drill_tests
        return drill_tests.cmd_test_drills(rest)
    if cmd == "test-cpu" and rest:
        import cpu_tests
        return cpu_tests.cmd_test_cpu(rest)
    if cmd == "test-recovery-combo" and rest:
        import recovery_combo_tests
        return recovery_combo_tests.cmd_test_recovery_combo(rest)
    if cmd == "test-stage3f" and rest:
        import stage3f_tests
        return stage3f_tests.cmd_test_stage3f(rest)
    if cmd == "test-scenarios" and rest:
        import scenario_presets_tests
        return scenario_presets_tests.cmd_test_scenarios(rest)
    if cmd == "test-display" and rest:
        import display_options_tests
        return display_options_tests.cmd_test_display(rest)
    if cmd == "test-controls" and rest:
        import control_tests
        return control_tests.cmd_test_controls(rest)
    if cmd == "test-di" and rest:
        import di_tests
        return di_tests.cmd_test_di(rest)
    if cmd == "test-frames" and rest:
        import frame_tests
        return frame_tests.cmd_test_frames(rest)
    if cmd == "test-savestate" and rest:
        import savestate_tests
        return savestate_tests.cmd_test_savestate(rest)
    if cmd == "test-charges" and rest:
        import charge_tests
        return charge_tests.cmd_test_charges(rest)
    if cmd == "test-record" and rest:
        import record_tests
        return record_tests.cmd_test_record(rest)
    if cmd == "test-extra-heaps" and rest:
        import extra_tests
        return extra_tests.cmd_test_extra_heaps(rest)
    if cmd == "test-fighter-sets" and rest:
        import fighter_set_tests
        return fighter_set_tests.cmd_test_fighter_sets(rest)
    if cmd == "test-fighter-swap" and rest:
        import fighter_swap_tests
        return fighter_swap_tests.cmd_test_fighter_swap(rest)
    if cmd == "test-storage" and rest:
        import storage_tests
        return storage_tests.cmd_test_storage(rest)
    if cmd == "test-card" and rest:
        import card_tests
        return card_tests.cmd_test_card(rest)
    if cmd == "test-sounds" and rest:
        import sound_tests
        return sound_tests.cmd_test_sounds(rest)
    if cmd == "test-rng" and rest:
        import rng_tests
        return rng_tests.cmd_test_rng(rest)
    if cmd == "test-fighter-mechanics" and rest:
        import fighter_mechanics_tests
        return fighter_mechanics_tests.cmd_test_fighter_mechanics(rest)
    if cmd == "test-stage-options" and rest:
        import stage_options_tests
        return stage_options_tests.cmd_test_stage_options(rest)
    if cmd == "test-stage3d" and rest:
        import stage3d_tests
        return stage3d_tests.cmd_test_stage3d(rest)
    if cmd == "test-stage5d" and rest:
        import stage5d_tests
        return stage5d_tests.cmd_test_stage5d(rest)
    if cmd == "test-stage4a-core" and rest:
        import stage4a_core_tests
        return stage4a_core_tests.cmd_test_stage4a_core(rest)
    if cmd == "test-stage4a-hover-ps" and rest:
        import stage4a_hover_ps_tests
        return stage4a_hover_ps_tests.cmd_test_stage4a_hover_ps(rest)
    if cmd == "test-stage4a-pplus-tech" and rest:
        import stage4a_pplus_tech_tests
        return stage4a_pplus_tech_tests.cmd_test_stage4a_pplus_tech(rest)
    if cmd == "test-skill-drills-ui" and rest:
        import skill_drills_ui_tests
        return skill_drills_ui_tests.cmd_test_skill_drills_ui(rest)
    if cmd == "test-grab-visualizer" and rest:
        import grab_visualizer_tests
        return grab_visualizer_tests.cmd_test_grab_visualizer(rest)
    if cmd == "test-psa-inspector" and rest:
        import psa_inspector_tests
        return psa_inspector_tests.cmd_test_psa_inspector(rest)
    if cmd == "test-hud-theme-meter" and rest:
        import hud_theme_meter_tests
        return hud_theme_meter_tests.cmd_test_hud_theme_meter(rest)
    if cmd == "test-match-hud" and rest:
        import match_hud_tests
        return match_hud_tests.cmd_test_match_hud(rest)
    if cmd == "test-code-menu" and rest:
        import code_menu_tests
        return code_menu_tests.cmd_test_code_menu(rest)
    if cmd == "test-perf" and rest:
        import perf_tests
        return perf_tests.cmd_test_perf(rest)
    if cmd == "test-harness" and rest:
        import harness_tests
        return harness_tests.cmd_test_harness(rest)
    raise SystemExit(__doc__)


if __name__ == "__main__":
    sys.exit(main())
