#!/usr/bin/env python3
"""Every regression check in one command.

    python3 tools/check.py                 # matching tests, match lock, native build + ctest
    python3 tools/check.py --sanitize      # also an AddressSanitizer/UBSan build and its tests (leaks too on Linux)
    python3 tools/check.py --replays DIR   # also play back every replay in DIR (needs your disc's assets)
    python3 tools/check.py --fast          # matching tests, the lock and the function map (what the git hook runs)

Steps that need something this machine lacks (the PSX toolchain for the lock, a C compiler for the native build,
replays, an llvm-mingw in CTR_LLVM_MINGW for the launcher's build recipe) say SKIP with the reason; a skip is never
counted as a pass. Exit 1 when any step failed.

On Windows the native builds use clang-cl in Visual Studio's x86 environment (vcvars32) with Ninja (else NMake):
plain MSVC can't compile the game's GCC statement expressions. CTR_CLANG_CL names another clang-cl. The match lock
runs on Linux only (the vendored PSX toolchain is Linux ELF), so on Windows it says SKIP: run it in WSL.
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
WINDOWS = platform.system() == "Windows"
# LeakSanitizer runs on Linux only: Windows ASan refuses detect_leaks ("not supported on this platform").
SANITIZER_ENV = {"ASAN_OPTIONS": "detect_leaks=1" if sys.platform.startswith("linux") else "abort_on_error=0"}


class WindowsTools:
    """Visual Studio's x86 environment, clang-cl and a generator for it, or the reason there is none."""

    def __init__(self) -> None:
        self.env: dict[str, str] | None = None
        self.clang: Path | None = None
        self.generator = ""
        self.why = ""
        try:
            self._find()
        except (OSError, subprocess.SubprocessError) as exc:
            self.env, self.why = None, f"Visual Studio's environment: {exc}"

    def _find(self) -> None:
        installer = Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / "Microsoft Visual Studio" / "Installer"
        roots: list[Path] = []
        if (installer / "vswhere.exe").exists():
            out = subprocess.run([str(installer / "vswhere.exe"), "-all", "-products", "*", "-requires",
                                  "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath"],
                                 capture_output=True, text=True, errors="replace", timeout=60).stdout
            roots = [Path(line.strip()) for line in out.splitlines() if line.strip()]
        vcvars = next((r / "VC" / "Auxiliary" / "Build" / "vcvars32.bat" for r in roots
                       if (r / "VC" / "Auxiliary" / "Build" / "vcvars32.bat").exists()), None)
        if vcvars is None:
            self.why = "Visual Studio with its C++ tools not found (vswhere)"
            return
        wanted = [Path(os.environ["CTR_CLANG_CL"])] if os.environ.get("CTR_CLANG_CL") else []
        wanted += [Path(os.environ.get("ProgramFiles", r"C:\Program Files")) / "LLVM" / "bin" / "clang-cl.exe"]
        for root in roots:
            wanted += [root / "VC" / "Tools" / "Llvm" / "bin" / "clang-cl.exe", root / "VC" / "Tools" / "Llvm" / "x64" / "bin" / "clang-cl.exe"]
        self.clang = next((p for p in wanted if p.is_file()), None)
        if self.clang is None and shutil.which("clang-cl"):
            self.clang = Path(shutil.which("clang-cl"))
        if self.clang is None:
            self.why = ("clang-cl not found: add Visual Studio's 'C++ Clang tools for Windows' or install LLVM "
                        "(plain MSVC can't compile the game's GCC statement expressions)")
            return
        done = subprocess.run(f'cmd /s /c ""{vcvars}" >nul 2>&1 && set"', capture_output=True, text=True,
                              errors="replace", timeout=300)
        env = dict(line.split("=", 1) for line in done.stdout.splitlines() if "=" in line and not line.startswith("="))
        path = env.get("PATH") or env.get("Path") or ""
        if done.returncode != 0 or not path:
            self.why = f"{vcvars.name} did not give an environment (exit {done.returncode})"
            return
        if shutil.which("cmake", path=path) is None:
            self.why = "cmake not found (Visual Studio's 'C++ CMake tools' part, or CMake on PATH)"
            return
        self.generator = "Ninja" if shutil.which("ninja", path=path) else "NMake Makefiles"
        self.env = env


def run(name: str, command: list[str], results: list, skip_codes: tuple[int, ...] = (), env: dict | None = None,
        base: dict | None = None) -> bool:
    print(f"== {name}: {' '.join(command)}", flush=True)
    started = time.monotonic()
    if base is not None and command and not Path(command[0]).is_absolute():
        command = [shutil.which(command[0], path=base.get("PATH") or base.get("Path")) or command[0], *command[1:]]
    done = subprocess.run(command, cwd=ROOT, env={**(base if base is not None else os.environ), **(env or {})})
    seconds = time.monotonic() - started
    if done.returncode in skip_codes:
        results.append((name, "SKIP", f"exit {done.returncode}", seconds))
        return True
    status = "PASS" if done.returncode == 0 else "FAIL"
    results.append((name, status, "" if done.returncode == 0 else f"exit {done.returncode}", seconds))
    return done.returncode == 0


def cache_value(build: Path, key: str) -> str | None:
    cache = build / "CMakeCache.txt"
    if not cache.exists():
        return None
    for line in cache.read_text(errors="replace").splitlines():
        if line.startswith(key + ":"):
            return line.split("=", 1)[1] if "=" in line else ""
    return None


def fresh_cache(build: Path, generator: str, compiler: str | None) -> None:
    """A build folder configured with another generator or compiler (an older check's) is configured afresh."""
    old_generator = cache_value(build, "CMAKE_GENERATOR")
    old_compiler = cache_value(build, "CMAKE_C_COMPILER")
    same_compiler = compiler is None or old_compiler is None or Path(old_compiler) == Path(compiler)
    if old_generator is not None and (old_generator != generator or not same_compiler):
        print(f"== {build.name}: configured with {old_generator} ({old_compiler}) before, starting it afresh", flush=True)
        (build / "CMakeCache.txt").unlink()
        shutil.rmtree(build / "CMakeFiles", ignore_errors=True)


def native_configure(build: Path, sanitize: str | None, tools: WindowsTools | None) -> list[str]:
    command = ["cmake", "-S", str(ROOT), "-B", str(build), "-DBUILD_TESTING=ON",
               "-DCMAKE_BUILD_TYPE=" + ("Debug" if sanitize else "Release")]
    if tools is not None:
        command += ["-G", tools.generator, f"-DCMAKE_C_COMPILER={tools.clang.as_posix()}", "-DCMAKE_C_FLAGS=-m32"]
    else:
        command += ["-DCMAKE_C_FLAGS=-m32", "-DCMAKE_EXE_LINKER_FLAGS=-m32"]
    if sanitize:
        command.append(f"-DCTR_NATIVE_SANITIZE={sanitize}")
    return command


def native(name: str, build: Path, sanitize: str | None, results: list, jobs: int, tools: WindowsTools | None) -> None:
    base = None
    if WINDOWS:
        if tools is None or tools.env is None:
            results.append((name, "SKIP", tools.why if tools else "no Windows toolchain", 0.0))
            return
        base = tools.env
        fresh_cache(build, tools.generator, tools.clang.as_posix())
    elif shutil.which("cmake") is None:
        results.append((name, "SKIP", "cmake not found", 0.0))
        return
    if not run(f"{name} configure", native_configure(build, sanitize, tools if WINDOWS else None), results, base=base):
        return
    if not run(f"{name} build", ["cmake", "--build", str(build), "-j", str(jobs)], results, base=base):
        return
    # The lock already ran on its own; ctest here covers the native tests.
    run(f"{name} ctest", ["ctest", "--test-dir", str(build), "--output-on-failure", "-E", "ctr_match_lock"],
        results, env=SANITIZER_ENV if sanitize else None, base=base)


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
    tools = WindowsTools() if WINDOWS else None
    # The function map checkpoints translate function pointers with: written from the game's sources, kept current.
    run("function map", [PY, "tools/gen_function_map.py", "--check"], results, skip_codes=(2,),
        env={"CC": str(tools.clang.with_name("clang.exe"))} if tools is not None and tools.clang is not None
        and tools.clang.with_name("clang.exe").exists() else None,
        base=tools.env if tools is not None and tools.env is not None else None)
    run("launcher unit tests", [PY, "-m", "unittest", "discover", "-s", "tools/launcher", "-p", "test_*.py"], results)
    build = ROOT / "build-check"
    if not args.fast:
        # The launcher package builds from tools/launcher/build-recipe.json, not CMakeLists.txt: they must agree.
        run("launcher build recipe", [PY, "tools/launcher/recipe.py", "check"], results, skip_codes=(2,))
        native("native", build, None, results, args.jobs, tools)
        if args.sanitize:
            # MSVC's AddressSanitizer has no UndefinedBehaviorSanitizer: address only there.
            sanitize = "address" if WINDOWS else args.sanitize
            native("sanitized", ROOT / "build-check-asan", sanitize, results, args.jobs, tools)
    if args.replays:
        exe = args.exe
        if exe is None:
            base = ROOT / ("build-check-asan" if args.sanitize else "build-check")
            exe = next((p for p in (base / "ctr_native", base / "Release" / "ctr_native.exe", base / "Debug" / "ctr_native.exe",
                                    base / "ctr_native.exe") if p.exists()), base / "ctr_native")
        run("replays", [PY, "tools/replay_check.py", "--exe", str(exe), "--replays", str(args.replays)], results, skip_codes=(2,),
            base=tools.env if tools is not None and tools.env is not None else None)

    print("\nCheck            Result  Time")
    for name, status, why, seconds in results:
        print(f"{name:<24} {status:<5} {seconds:6.1f}s {why}")
    failed = [r for r in results if r[1] == "FAIL"]
    skipped = [r for r in results if r[1] == "SKIP"]
    print(f"\n{len(results) - len(failed) - len(skipped)} passed, {len(failed)} failed, {len(skipped)} skipped")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
