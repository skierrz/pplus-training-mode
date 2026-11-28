#!/usr/bin/env python3
"""Release readiness gate for Project+ Training Mode Mod (P+TMM).

Runs and validates:
  1. All 3 build configurations (Normal, PERF=1, STAGE4C=0):
     - zero MWCC/MWLD compiler or linker warnings
     - stub, match, menu, extra, and load temp buffer footprints within budget
     - PERF=1 match headroom >= --min-perf-main-headroom (default 512 B)
     - packed build/TrainingMode.rel size and SHA-256
  2. Host unit tests (make check-host), passing HOST_DUMP when provided.
  3. All generator freshness checks (scripts/gen_*.py --check).
  4. Public repository hygiene scan (Unicode dashes, local paths, stray controls)
     plus optional external --hygiene-script hook.
"""

import argparse
import glob
import hashlib
import os
import re
import shutil
import subprocess
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

CONFIGS = [
    ("Normal", [], os.path.join("build", "TrainingMode.rel")),
    ("PERF=1", ["PERF=1"], os.path.join("build", "perf", "TrainingMode.rel")),
    ("STAGE4C=0", ["STAGE4C=0"], os.path.join("build", "no-stage4c", "TrainingMode.rel")),
]

RE_STUB = re.compile(
    r"^size\s+stub\s+\d+\s+bytes,\s+fixSize\s+0x[0-9a-fA-F]+\s+\+\s+\.bss\s+0x[0-9a-fA-F]+:\s+"
    r"(\d+)\s+bytes loaded(?:,\s+budget\s+(\d+))?",
    re.M,
)
RE_MOD = re.compile(
    r"^size\s+(match|menu|extra)\s+module\s+\d+\s+bytes,\s+fixSize\s+0x[0-9a-fA-F]+\s+\+\s+\.bss\s+0x[0-9a-fA-F]+:\s+"
    r"(\d+)\s+bytes loaded(?:,\s+budget\s+(\d+))?",
    re.M,
)
RE_TEMP = re.compile(
    r"^size\s+load temp buffer\s+(\d+)\s+bytes\s+\([^)]+\)(?:,\s+budget\s+(\d+))?",
    re.M,
)
RE_WARNING = re.compile(
    r"(?:^|\s)(?:###\s*(?:mwcc|mwld)\S*\s+.*warning|warning\s*:|Warning\s*:|\bWarning\b\s*:)",
    re.I,
)

# Built-in hygiene patterns (constructed without embedding forbidden literals).
EM_DASH = chr(0x2014)
EN_DASH = chr(0x2013)
RE_LOCAL_PATH = re.compile(
    r"(?:[A-Za-z]:[\\/]" + "Us" + r"ers[\\/]|/" + "Us" + r"ers/|/ho" + r"me/)[A-Za-z0-9._-]+"
)
SKIP_PREFIXES = ("lib/", "assets/", "build/", "tools/")
TEXT_EXTS = {
    ".c", ".cc", ".cpp", ".h", ".hpp", ".py", ".sh", ".md", ".txt", ".mk", ".lcf", ".json", ".yml", ".yaml",
}


def build_env():
    env = os.environ.copy()
    env["MSYS2_ARG_CONV_EXCL"] = "*"
    if os.name == "nt":
        extra_paths = []
        for p in ("C:\\devkitPro\\msys2\\usr\\bin", "C:\\devkitPro\\devkitPPC\\bin"):
            if os.path.isdir(p) and p.lower() not in env.get("PATH", "").lower():
                extra_paths.append(p)
        if extra_paths:
            env["PATH"] = os.pathsep.join(extra_paths) + os.pathsep + env.get("PATH", "")
    return env


def resolve_make(requested, env):
    if requested:
        return requested
    found = shutil.which("make", path=env.get("PATH"))
    if found:
        return found
    win_make = "C:\\devkitPro\\msys2\\usr\\bin\\make.exe"
    if os.name == "nt" and os.path.exists(win_make):
        return win_make
    return "make"


def run_cmd(cmd, env, cwd=REPO_ROOT):
    proc = subprocess.run(
        cmd,
        cwd=cwd,
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    return proc.returncode, proc.stdout


def parse_pack_sizes(output):
    sizes = {}
    m = RE_STUB.search(output)
    if m:
        sizes["stub"] = (int(m.group(1)), int(m.group(2) or 0))
    for m in RE_MOD.finditer(output):
        sizes[m.group(1)] = (int(m.group(2)), int(m.group(3) or 0))
    m = RE_TEMP.search(output)
    if m:
        sizes["temp"] = (int(m.group(1)), int(m.group(2) or 0))
    return sizes


def find_warnings(output):
    hits = []
    for line in output.splitlines():
        s = line.strip()
        if not s:
            continue
        if RE_WARNING.search(s):
            hits.append(s)
    return hits


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def check_builds(make_cmd, env, args):
    results = []
    size_rows = []
    rel_info = None
    all_ok = True

    if args.clean:
        rc, out = run_cmd([make_cmd, "clean"], env)
        if rc != 0:
            return False, [("make clean", "FAIL", f"exit {rc}")], [], None, out

    for label, extra_vars, rel_relpath in CONFIGS:
        rel_abs = os.path.join(REPO_ROOT, rel_relpath)
        # Remove packed .rel so pack-rel.py always runs and reports fresh footprints.
        if os.path.exists(rel_abs):
            try:
                os.remove(rel_abs)
            except OSError:
                pass
        cmd = [make_cmd, "--no-print-directory", f"-j{args.jobs}"] + extra_vars + ["all"]
        rc, out = run_cmd(cmd, env)
        warnings = find_warnings(out)
        sizes = parse_pack_sizes(out)

        errors = []
        if rc != 0:
            errors.append(f"make exit {rc}")
        if warnings:
            errors.append(f"{len(warnings)} warning(s)")
        for mod in ("stub", "match", "menu", "extra", "temp"):
            if mod not in sizes:
                errors.append(f"missing {mod} size")
                continue
            used, budget = sizes[mod]
            headroom = budget - used if budget else 0
            size_rows.append((label, mod, used, budget, headroom))
            if budget and used > budget:
                errors.append(f"{mod} over budget by {used - budget} B")
        if label == "PERF=1" and "match" in sizes:
            used, budget = sizes["match"]
            headroom = budget - used
            if headroom < args.min_perf_main_headroom:
                errors.append(
                    f"PERF=1 match headroom +{headroom} B < required +{args.min_perf_main_headroom} B"
                )
        if not os.path.exists(rel_abs):
            errors.append(f"missing {rel_relpath}")
        elif label == "Normal":
            rel_info = (rel_relpath, os.path.getsize(rel_abs), sha256_file(rel_abs))

        ok = len(errors) == 0
        all_ok = all_ok and ok
        if ok:
            m_used, m_bud = sizes["match"]
            menu_used, menu_bud = sizes["menu"]
            detail = f"0 warnings | match +{m_bud - m_used} B | menu +{menu_bud - menu_used} B"
        else:
            detail = "; ".join(errors)
            if warnings:
                detail += f" (first: {warnings[0][:90]})"
            elif rc != 0:
                tail = [ln.strip() for ln in out.splitlines() if ln.strip()][-3:]
                if tail:
                    detail += " | " + " / ".join(tail)[:140]
        results.append((f"build ({label})", "PASS" if ok else "FAIL", detail))

    return all_ok, results, size_rows, rel_info, ""


def check_host_tests(make_cmd, env, host_dump):
    cmd = [make_cmd, "--no-print-directory", "check-host"]
    if host_dump:
        cmd.append(f"HOST_DUMP={host_dump}")
    rc, out = run_cmd(cmd, env)
    summary_line = ""
    for line in reversed(out.splitlines()):
        s = line.strip()
        if s.startswith("host tests:"):
            summary_line = s
            break
    if not summary_line:
        for line in reversed(out.splitlines()):
            s = line.strip()
            if s and not s.startswith("make["):
                summary_line = s
                break
    ok = rc == 0
    if not ok:
        tail = [ln.strip() for ln in out.splitlines() if ln.strip() and not ln.strip().startswith("make[")][-4:]
        summary_line = " | ".join(tail)[:160]
    return ok, ("host tests", "PASS" if ok else "FAIL", summary_line or f"exit {rc}")


def check_generators(env):
    scripts = sorted(glob.glob(os.path.join(REPO_ROOT, "scripts", "gen_*.py")))
    if not scripts:
        return False, [("generators", "FAIL", "no scripts/gen_*.py found")]
    all_ok = True
    rows = []
    for sp in scripts:
        name = os.path.relpath(sp, REPO_ROOT).replace(os.sep, "/")
        rc, out = run_cmd([sys.executable, sp, "--check"], env)
        ok = rc == 0
        all_ok = all_ok and ok
        msg = "up to date" if ok else (out.strip().splitlines()[-1][:120] if out.strip() else f"exit {rc}")
        rows.append((f"gen ({os.path.basename(sp)})", "PASS" if ok else "FAIL", msg))
    return all_ok, rows


def tracked_files(env):
    rc, out = run_cmd(["git", "ls-files", "--cached", "--others", "--exclude-standard"], env)
    if rc == 0 and out.strip():
        return [line.strip() for line in out.splitlines() if line.strip()]
    found = []
    for root, dirs, files in os.walk(REPO_ROOT):
        dirs[:] = [d for d in dirs if d not in (".git", "build", "lib", "assets", "tools")]
        for fn in sorted(files):
            rel = os.path.relpath(os.path.join(root, fn), REPO_ROOT).replace(os.sep, "/")
            found.append(rel)
    return found


def is_text_candidate(rel):
    if any(rel.startswith(p) for p in SKIP_PREFIXES):
        return False
    base = os.path.basename(rel)
    if base in ("Makefile", "LICENSE", "COPYING", "CREDITS"):
        return True
    _, ext = os.path.splitext(base)
    return ext.lower() in TEXT_EXTS


def check_hygiene(env, hygiene_script):
    findings = []
    files = tracked_files(env)
    checked = 0
    for rel in files:
        if not is_text_candidate(rel):
            continue
        full = os.path.join(REPO_ROOT, rel)
        if not os.path.isfile(full):
            continue
        checked += 1
        try:
            raw = open(full, "rb").read()
        except OSError as exc:
            findings.append(f"{rel}: read error: {exc}")
            continue
        if b"\x00" in raw:
            continue
        text = raw.decode("utf-8", errors="replace")
        for lineno, line in enumerate(text.splitlines(), 1):
            if EM_DASH in line or EN_DASH in line:
                findings.append(f"{rel}:{lineno}: Unicode em/en dash found")
            if RE_LOCAL_PATH.search(line):
                findings.append(f"{rel}:{lineno}: hardcoded local user path found")
            for ch in line:
                code = ord(ch)
                if code < 32 and ch not in ("\t", "\r", "\n"):
                    # Intentional single-byte label markers in C/C++ strings use escapes; raw control bytes are bad.
                    findings.append(f"{rel}:{lineno}: stray control char 0x{code:02X}")
                    break

    rows = []
    builtin_ok = len(findings) == 0
    if builtin_ok:
        rows.append(("hygiene (built-in)", "PASS", f"{checked} tracked files clean"))
    else:
        sample = "; ".join(findings[:3])
        rows.append(("hygiene (built-in)", "FAIL", f"{len(findings)} issue(s): {sample}"))

    ext_ok = True
    if hygiene_script:
        rc, out = run_cmd([sys.executable, hygiene_script, REPO_ROOT], env)
        ext_ok = rc == 0
        last = out.strip().splitlines()[-1][:120] if out.strip() else f"exit {rc}"
        rows.append(("hygiene (external)", "PASS" if ext_ok else "FAIL", last))

    return builtin_ok and ext_ok, rows


def print_table(title, headers, rows):
    widths = [len(h) for h in headers]
    for r in rows:
        for i, v in enumerate(r):
            widths[i] = max(widths[i], len(str(v)))
    sep = "+-" + "-+-".join("-" * w for w in widths) + "-+"
    print(f"\n{title}")
    print(sep)
    print("| " + " | ".join(str(headers[i]).ljust(widths[i]) for i in range(len(headers))) + " |")
    print(sep)
    for r in rows:
        print("| " + " | ".join(str(r[i]).ljust(widths[i]) for i in range(len(headers))) + " |")
    print(sep)


def main():
    ap = argparse.ArgumentParser(description="Verify release readiness for P+TMM.")
    ap.add_argument("--clean", action="store_true", help="Run make clean before building all 3 configs.")
    ap.add_argument("--jobs", type=int, default=4, help="Parallel make jobs (default: 4).")
    ap.add_argument("--make", dest="make_cmd", default="", help="Override make executable path.")
    ap.add_argument(
        "--min-perf-main-headroom",
        type=int,
        default=512,
        help="Minimum required PERF=1 match headroom in bytes (default: 512).",
    )
    ap.add_argument(
        "--host-dump",
        default=os.environ.get("TM_HOST_DUMP") or os.environ.get("HOST_DUMP") or "",
        help="Optional MEM1 dump path forwarded as HOST_DUMP to make check-host.",
    )
    ap.add_argument(
        "--hygiene-script",
        default=os.environ.get("HYGIENE_SCRIPT", ""),
        help="Optional external hygiene verification script path.",
    )
    ap.add_argument("--skip-host", action="store_true", help="Skip make check-host.")
    args = ap.parse_args()

    env = build_env()
    make_cmd = resolve_make(args.make_cmd, env)

    summary_rows = []
    overall_ok = True

    builds_ok, build_rows, size_rows, rel_info, clean_err = check_builds(make_cmd, env, args)
    overall_ok = overall_ok and builds_ok
    summary_rows.extend(build_rows)

    if not args.skip_host:
        host_ok, host_row = check_host_tests(make_cmd, env, args.host_dump)
        overall_ok = overall_ok and host_ok
        summary_rows.append(host_row)

    gen_ok, gen_rows = check_generators(env)
    overall_ok = overall_ok and gen_ok
    summary_rows.extend(gen_rows)

    hyg_ok, hyg_rows = check_hygiene(env, args.hygiene_script)
    overall_ok = overall_ok and hyg_ok
    summary_rows.extend(hyg_rows)

    if size_rows:
        fmt_sizes = [
            (cfg, mod, f"{used:,} B", f"{bud:,} B", f"{'+' if hr >= 0 else ''}{hr:,} B")
            for cfg, mod, used, bud, hr in size_rows
        ]
        print_table("Module Footprints", ["Config", "Module", "Loaded", "Budget", "Headroom"], fmt_sizes)

    if rel_info:
        rel_path, rel_size, rel_sha = rel_info
        print(f"\nArtifact: {rel_path}")
        print(f"  Size  : {rel_size:,} bytes")
        print(f"  SHA256: {rel_sha}")

    print_table("Release Gate Summary", ["Check", "Status", "Detail"], summary_rows)
    if clean_err:
        print(clean_err)

    print(f"\nVERIFY-RELEASE: {'PASS' if overall_ok else 'FAIL'}")
    return 0 if overall_ok else 1


if __name__ == "__main__":
    sys.exit(main())
