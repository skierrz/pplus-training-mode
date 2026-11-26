"""Builds the PC tests of the mod's logic and runs them (`make check-host`).

usage: build.py [--dump PREFIX-OR-FILE] [--only TEXT] [--log] [--list]
                [--no-run] [--verbose]

Selected sources of the mod (MOD_SOURCES below) are compiled as they are with
the PC's own C++ compiler, against the stand-in headers in tests/host/include
and the test layer in tests/host/host, together with the tests in
tests/host/tests, into build/host/host_tests.exe, which is then run. See
docs/testing.md ("Tests on the PC").

The compiler is Visual Studio's 32-bit one (the mod keeps game addresses in
pointers, so the test program must be 32-bit like the console). It is found
at run time with vswhere, or taken from the environment when this is run from
a Visual Studio x86 command prompt. Without one this says so and exits with
status 0: the mod's own build does not depend on it.

--dump, --only, --log and --list go to the test program. TM_HOST_DUMP in the
environment does what --dump does.
"""
import glob
import json
import os
import shutil
import subprocess
import sys
import time

sys.dont_write_bytecode = True  # no __pycache__ in the repo

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)  # text_scan.py and text_rules.py
ROOT = os.path.dirname(os.path.dirname(HERE))
OUT = os.path.join(ROOT, "build", "host")

# The mod's sources in the PC build, by path under src/. The module a source
# belongs to (its TM_MODULE_* define) follows from its folder. A source is
# added here once the stand-in headers cover what it includes; what it calls
# in sources that are not here is answered by tests/host/host/fakes.cpp.
# src/extra/frame_data.cpp is not in this list because its test includes it
# (tests/host/tests/test_frame_data.cpp), to reach its file-local functions;
# src/main/ui/osd.cpp, src/menu/drill_list.cpp and src/menu/browser.cpp are
# included by tests/host/tests/test_text_fit.cpp for the same reason.
MOD_SOURCES = [
    "extra/fighter_mechanics.cpp",
    "extra/menu_descriptions.cpp",
    "extra/pplus_recovery_core.cpp",
    "extra/scenario_presets_core.cpp",
    "main/cpu/actions.cpp",
    "main/cpu/lab_data.cpp",
    "main/extra.cpp",
    "main/game/code_menu.cpp",
    "main/game/pad_inmatch.cpp",
    "main/game/status_names.cpp",
    "main/game/tech_frames.cpp",
    "main/game/training_controller.cpp",
    "main/game/training_damage.cpp",
    "main/gfx/text.cpp",
    "main/settings.cpp",
    "main/storage/areas.cpp",
    "main/storage/file_io.cpp",
    "main/storage/notice.cpp",
    "main/storage/recording_file.cpp",
    "main/storage/settings_file.cpp",
    "main/storage/storage.cpp",
    "main/record/hud.cpp",
    "main/record/slots.cpp",
    "main/ui/hud_layout.cpp",
    "main/ui/lab_menu.cpp",
    "main/ui/menu.cpp",
    "main/ui/pages/*.cpp",  # every page of the lab menu
    "main/ui/settings_modal.cpp",
    "main/ui/watermark.cpp",
    "menu/game/training.cpp",
    "menu/names.cpp",
    "menu/bests_file.cpp",
    "menu/osd_quick.cpp",
    "menu/session.cpp",
    "stub/hook.cpp",
    "stub/inline_stub.cpp",
    "stub/packed_file.cpp",
]

MODULE_DEFINES = {"main": "TM_MODULE_MAIN", "extra": "TM_MODULE_EXTRA", "menu": "TM_MODULE_MENU",
                  "stub": "TM_MODULE_STUB", "shared": "TM_MODULE_MAIN"}

# The longest the test program may run (it needs well under a second).
RUN_SECONDS = 120

NO_COMPILER = """check-host: SKIPPED: no PC C++ compiler was found.
  The PC tests need Visual Studio 2022 (or its Build Tools) with the
  "Desktop development with C++" workload, which brings the 32-bit (x86)
  compiler. The mod itself builds and runs without it."""


def vcvars_path():
    """Visual Studio's 32-bit environment script, or None."""
    # vswhere lives in the 32-bit program folder. The shell make starts does
    # not pass the program folders' variables on, so Windows is asked too.
    folders = [os.environ.get("ProgramFiles(x86)"), os.environ.get("ProgramFiles")]
    try:
        import winreg
        with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\Microsoft\Windows\CurrentVersion") as key:
            for value in ("ProgramFilesDir (x86)", "ProgramFilesDir"):
                try:
                    folders.append(winreg.QueryValueEx(key, value)[0])
                except OSError:
                    pass
    except (ImportError, OSError):
        pass
    candidates = [os.path.join(f, "Microsoft Visual Studio", "Installer", "vswhere.exe") for f in folders if f]
    candidates.append(shutil.which("vswhere") or "")
    for vswhere in candidates:
        if not os.path.isfile(vswhere):
            continue
        try:
            found = subprocess.run(
                [vswhere, "-latest", "-products", "*", "-requires",
                 "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath"],
                capture_output=True, text=True, timeout=60).stdout.strip().splitlines()
        except (OSError, subprocess.SubprocessError):
            continue
        for install in found:
            script = os.path.join(install.strip(), "VC", "Auxiliary", "Build", "vcvars32.bat")
            if os.path.isfile(script):
                return script
    return None


def compiler_environment():
    """(cl's path, the environment to run it in), or None without a 32-bit compiler."""
    env = dict(os.environ)
    here = shutil.which("cl")
    if here and env.get("VSCMD_ARG_TGT_ARCH", "").lower() == "x86":
        return here, env  # a Visual Studio x86 command prompt
    script = vcvars_path()
    if script is None:
        return None
    # The script takes a second or two; its result is kept until it changes.
    cache = os.path.join(OUT, "compiler-env.json")
    stamp = "%s|%d" % (script, os.path.getmtime(script))
    try:
        with open(cache, "r", encoding="utf-8") as f:
            kept = json.load(f)
        if kept.get("stamp") == stamp and os.path.isfile(kept.get("cl", "")):
            return kept["cl"], kept["env"]
    except (OSError, ValueError):
        pass
    # Our own PATH may hold other tools named link.exe or cl: start from the
    # system's.
    base = dict(os.environ)
    system = os.environ.get("SystemRoot")
    if system:
        base["PATH"] = os.pathsep.join([os.path.join(system, "System32"), system])
    try:
        shown = subprocess.run('"%s" >nul 2>&1 && set' % script, shell=True, capture_output=True, text=True,
                               env=base, timeout=120).stdout
    except (OSError, subprocess.SubprocessError):
        return None
    env = {}
    for line in shown.splitlines():
        name, sep, value = line.partition("=")
        if sep and name:
            env[name] = value
    path = env.get("PATH") or env.get("Path") or ""
    cl = shutil.which("cl", path=path)
    if not cl:
        return None
    os.makedirs(OUT, exist_ok=True)
    with open(cache, "w", encoding="utf-8") as f:
        json.dump({"stamp": stamp, "cl": cl, "env": env}, f)
    return cl, env


def sources(folder):
    return sorted(os.path.join(folder, n) for n in os.listdir(folder) if n.endswith(".cpp"))


def write_if_changed(path, text):
    try:
        with open(path, "r", encoding="utf-8") as f:
            if f.read() == text:
                return
    except OSError:
        pass
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)


def wrappers():
    """One small file per mod source: its module's define, then the source.

    The mod has sources of the same name in different folders; the wrappers'
    names are unique, so their objects can share a folder.
    """
    gen = os.path.join(OUT, "gen")
    os.makedirs(gen, exist_ok=True)
    wanted = {}
    listed = []
    for entry in MOD_SOURCES:
        found = sorted(glob.glob(os.path.join(ROOT, "src", *entry.split("/"))))
        if not found:
            sys.exit("check-host: %s is listed in tests/host/build.py but nothing in src/ matches it" % entry)
        listed += [os.path.relpath(f, os.path.join(ROOT, "src")).replace(os.sep, "/") for f in found]
    for rel in listed:
        define = MODULE_DEFINES[rel.split("/")[0]]
        name = "src_" + rel[:-len(".cpp")].replace("/", "_") + ".cpp"
        wanted[name] = '#define %s 1\n#include "%s"\n' % (define, rel)
    # The texts the sources draw, for test_text_fit.cpp (host/text_items.h).
    import text_rules
    wanted["text_items.cpp"] = text_rules.table(os.path.join(ROOT, "src"))[0]
    for name in os.listdir(gen):
        if name not in wanted:
            os.remove(os.path.join(gen, name))
    for name, text in wanted.items():
        write_if_changed(os.path.join(gen, name), text)
    return [os.path.join(gen, name) for name in sorted(wanted)]


def build(cl, env, verbose):
    obj = os.path.join(OUT, "obj")
    shutil.rmtree(obj, ignore_errors=True)
    os.makedirs(obj)
    exe = os.path.join(OUT, "host_tests.exe")
    files = wrappers() + sources(os.path.join(HERE, "host")) + sources(os.path.join(HERE, "tests"))
    command = [
        cl, "/nologo", "/MP", "/Od", "/W3", "/EHsc", "/fp:precise", "/arch:SSE2",
        "/wd4068",  # unknown pragma: the console compiler's (force_active)
        "/D_CRT_SECURE_NO_WARNINGS",
        "/FIhost_prefix.h",
        "/I" + os.path.join(HERE, "include"), "/I" + HERE, "/I" + os.path.join(ROOT, "src"),
        "/Fo" + obj + os.sep, "/Fd" + os.path.join(OUT, "host_tests.pdb"), "/Fe" + exe,
    ] + files + ["/link", "/LARGEADDRESSAWARE", "/INCREMENTAL:NO"]
    if verbose:
        print(" ".join(command))
    # The linker's temporary files: a folder of ours, whatever the shell
    # this was started from has (or does not have) in TMP.
    env = dict(env)
    env["TMP"] = env["TEMP"] = os.path.join(OUT, "tmp")
    os.makedirs(env["TMP"], exist_ok=True)
    done = subprocess.run(command, cwd=ROOT, env=env, capture_output=True, text=True, errors="replace")
    text = done.stdout + done.stderr
    # cl names every file it compiles; only what it says about them matters.
    names = {os.path.basename(f) for f in files}
    lines = [l for l in text.splitlines() if l.strip() and l.strip() not in names and l.strip() != "Generating Code..."]
    if done.returncode != 0 or verbose:
        print("\n".join(lines))
    elif lines:
        print("\n".join(l for l in lines if "warning" in l))
    if done.returncode != 0:
        print("check-host: the PC build FAILED (%d files)" % len(files))
        return None
    return exe


def main(argv):
    passed = []
    run = True
    verbose = False
    i = 0
    while i < len(argv):
        a = argv[i]
        if a == "--no-run":
            run = False
        elif a == "--verbose":
            verbose = True
        elif a in ("--dump", "--only") and i + 1 < len(argv):
            passed += [a, argv[i + 1]]
            i += 1
        elif a in ("--log", "--list"):
            passed.append(a)
        else:
            print(__doc__)
            return 2
        i += 1
    start = time.time()
    found = compiler_environment()
    if found is None:
        print(NO_COMPILER)
        return 0
    exe = build(found[0], found[1], verbose)
    if exe is None:
        return 1
    built = time.time()
    if not run:
        print("check-host: built %s in %.1f s" % (os.path.relpath(exe, ROOT), built - start))
        return 0
    sys.stdout.flush()
    try:
        status = subprocess.run([exe] + passed, cwd=ROOT, timeout=RUN_SECONDS).returncode
    except subprocess.TimeoutExpired:
        # The whole run takes under a second: this is a loop that never ends.
        print("check-host: the test program did not end within %d s and was stopped. The test after the last "
              "one listed above never finished." % RUN_SECONDS)
        return 1
    if status not in (0, 1, 2):
        print("check-host: the test program stopped without finishing (status %#x)" % (status & 0xFFFFFFFF))
        status = 1
    print("check-host: build %.1f s, tests %.1f s" % (built - start, time.time() - built))
    return status


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
