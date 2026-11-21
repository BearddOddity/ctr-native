#!/usr/bin/env python3
"""Every regression check in one command.

    python3 tools/check.py                 # matching tests, match lock, native build + ctest
    python3 tools/check.py --sanitize      # also an AddressSanitizer/UBSan build and its tests (leaks too on Linux)
    python3 tools/check.py --replays DIR   # also play back every replay in DIR (needs your disc's assets)
    python3 tools/check.py --fast          # matching tests and the lock only (what the git hook runs)

Steps that need something this machine lacks (the PSX toolchain for the lock, a C compiler for the native build,
replays) say SKIP with the reason; a skip is never counted as a pass. Exit 1 when any step failed.
"""
from __future__ import annotations

import argparse
import os
import platform
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PY = sys.executable


def run(name: str, command: list[str], results: list, skip_codes: tuple[int, ...] = (), env: dict | None = None) -> bool:
    print(f"== {name}: {' '.join(command)}", flush=True)
    started = time.monotonic()
    done = subprocess.run(command, cwd=ROOT, env={**os.environ, **(env or {})})
    seconds = time.monotonic() - started
    if done.returncode in skip_codes:
        results.append((name, "SKIP", f"exit {done.returncode}", seconds))
        return True
    status = "PASS" if done.returncode == 0 else "FAIL"
    results.append((name, status, "" if done.returncode == 0 else f"exit {done.returncode}", seconds))
    return done.returncode == 0


def native_configure(build: Path, sanitize: str | None) -> list[str]:
    command = ["cmake", "-S", str(ROOT), "-B", str(build), "-DBUILD_TESTING=ON"]
    if platform.system() == "Windows":
        command += ["-A", "Win32"]
    else:
        command += ["-DCMAKE_BUILD_TYPE=" + ("Debug" if sanitize else "Release"),
                    "-DCMAKE_C_FLAGS=-m32", "-DCMAKE_EXE_LINKER_FLAGS=-m32"]
    if sanitize:
        command.append(f"-DCTR_NATIVE_SANITIZE={sanitize}")
    return command


def native(name: str, build: Path, sanitize: str | None, results: list, jobs: int) -> None:
    if shutil.which("cmake") is None:
        results.append((name, "SKIP", "cmake not found", 0.0))
        return
    if not run(f"{name} configure", native_configure(build, sanitize), results):
        return
    config = ["--config", "Debug" if sanitize else "Release"] if platform.system() == "Windows" else []
    if not run(f"{name} build", ["cmake", "--build", str(build), "-j", str(jobs), *config], results):
        return
    # The lock already ran on its own; ctest here covers the native tests.
    run(f"{name} ctest", ["ctest", "--test-dir", str(build), "--output-on-failure", "-E", "ctr_match_lock", *(["-C", config[1]] if config else [])],
        results, env={"ASAN_OPTIONS": "detect_leaks=1"} if sanitize else None)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--fast", action="store_true", help="matching tests and the match lock only")
    parser.add_argument("--sanitize", nargs="?", const="address;undefined", default=None,
                        help='add a sanitizer build (default "address;undefined")')
    parser.add_argument("--replays", type=Path, help="play back every *.ctrreplay here with the checked build")
    parser.add_argument("--exe", type=Path, help="the ctr_native for --replays (default: this check's build)")
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    args = parser.parse_args(argv)

    results: list[tuple[str, str, str, float]] = []
    run("matching unit tests", [PY, "-m", "unittest", "discover", "-s", "tools/matching", "-p", "test_*.py"], results)
    run("match lock", [PY, "tools/matching/lock.py", "check"], results, skip_codes=(2,))
    build = ROOT / "build-check"
    if not args.fast:
        native("native", build, None, results, args.jobs)
        if args.sanitize:
            native("sanitized", ROOT / "build-check-asan", args.sanitize, results, args.jobs)
    if args.replays:
        exe = args.exe
        if exe is None:
            base = ROOT / ("build-check-asan" if args.sanitize else "build-check")
            exe = next((p for p in (base / "ctr_native", base / "Release" / "ctr_native.exe", base / "Debug" / "ctr_native.exe",
                                    base / "ctr_native.exe") if p.exists()), base / "ctr_native")
        run("replays", [PY, "tools/replay_check.py", "--exe", str(exe), "--replays", str(args.replays)], results, skip_codes=(2,))

    print("\nCheck            Result  Time")
    for name, status, why, seconds in results:
        print(f"{name:<24} {status:<5} {seconds:6.1f}s {why}")
    failed = [r for r in results if r[1] == "FAIL"]
    skipped = [r for r in results if r[1] == "SKIP"]
    print(f"\n{len(results) - len(failed) - len(skipped)} passed, {len(failed)} failed, {len(skipped)} skipped")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
