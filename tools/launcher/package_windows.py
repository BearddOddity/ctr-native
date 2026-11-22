#!/usr/bin/env python3
"""Make the CTR Launcher release folder: everything a player needs, nothing to install.

    python tools/launcher/package_windows.py [--out dist/CTR-Launcher] [--zip]

The folder it writes:

    CTR Launcher.exe     the launcher (launcher/: Tauri, Bun and Rust; its menus look like the game's)
    source/              the game's source (what Play recompiles)
    tools/llvm-mingw/    the compiler (clang for 32-bit Windows, with its own headers and libraries)
    tools/cmake/         CMake
    tools/ninja/         Ninja
    README.txt

The player drops their own CTR disc (NTSC-U .bin/.cue) on the launcher and presses
Play: it links the disc into assets/, recompiles the game into build/ with the
bundled tools and starts it. No game data is ever put in the package.

The tools are downloaded once into --cache and checked against
tools/launcher/toolchain.lock.json (sha256). A tool with no entry there yet has its
hash written in on first download (commit the file afterwards); a mismatch stops.
The launcher is built from launcher/ with Bun and Cargo (`bun install`, then
`tauri build`): run it on Windows with both on PATH, or pass a launcher built some
other way with --launcher-exe. Off Windows it cross-builds for x86_64-pc-windows-gnu
(rustup target add x86_64-pc-windows-gnu, and mingw-w64), which also needs
WebView2Loader.dll beside the exe; it is copied in. The window is Microsoft Edge
WebView2, which Windows 10 and 11 already have.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import urllib.request
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
LOCK = Path(__file__).with_name("toolchain.lock.json")

# Pinned tool releases (override with the flags below).
LLVM_MINGW = "20241217"
CMAKE = "3.31.6"
NINJA = "1.12.1"

# What Play compiles: the game, its platform layer and SDL (the launcher is built here, not by Play).
SOURCE_PATHS = [
    "CMakeLists.txt",
    "main.c",
    "LICENSE",
    "THIRD_PARTY_NOTICES.md",
    "README.md",
    "game/",
    "include/",
    "platform/",
    "externals/SDL/",
]

# llvm-mingw ships every target; the game only needs 32-bit x86.
LLVM_MINGW_DROP = ["aarch64-w64-mingw32", "armv7-w64-mingw32", "x86_64-w64-mingw32", "arm64ec-w64-mingw32"]
CMAKE_DROP = ["doc", "man"]

# Made by playing from the folder (the player's disc, builds, logs, settings): never packaged.
RUN_ONLY = ["assets", "build", "logs", "memcards", "ctr_native.ini", "launcher.ini"]


def tool_urls(args: argparse.Namespace) -> dict[str, str]:
    v = args.llvm_mingw
    return {
        "llvm-mingw": f"https://github.com/mstorsjo/llvm-mingw/releases/download/{v}/llvm-mingw-{v}-ucrt-x86_64.zip",
        "cmake": f"https://github.com/Kitware/CMake/releases/download/v{args.cmake}/cmake-{args.cmake}-windows-x86_64.zip",
        "ninja": f"https://github.com/ninja-build/ninja/releases/download/v{args.ninja}/ninja-win.zip",
    }


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def load_lock() -> dict[str, str]:
    if LOCK.exists():
        return json.loads(LOCK.read_text(encoding="utf-8"))
    return {}


def fetch(url: str, cache: Path, lock: dict[str, str], offline: bool) -> Path:
    cache.mkdir(parents=True, exist_ok=True)
    dest = cache / url.rsplit("/", 1)[1]
    if not dest.exists():
        if offline:
            raise SystemExit(f"not in the cache (and --offline): {dest}")
        print(f"downloading {url}")
        tmp = dest.with_suffix(dest.suffix + ".part")
        with urllib.request.urlopen(url) as r, tmp.open("wb") as f:
            shutil.copyfileobj(r, f)
        tmp.replace(dest)
    digest = sha256(dest)
    known = lock.get(url)
    if known is None:
        lock[url] = digest
        print(f"  sha256 {digest} (new: recorded in {LOCK.name}; commit it)")
    elif known != digest:
        raise SystemExit(f"sha256 mismatch for {dest.name}: {digest}, the lock says {known}. Delete it and retry.")
    else:
        print(f"  sha256 ok ({dest.name})")
    return dest


def unzip_into(archive: Path, dest: Path, drop: list[str]) -> None:
    """Extracts a zip whose files sit under one top folder into dest, minus drop."""
    if dest.exists():
        shutil.rmtree(dest)
    dest.mkdir(parents=True)
    with zipfile.ZipFile(archive) as z:
        names = [n for n in z.namelist() if not n.endswith("/")]
        tops = {n.split("/", 1)[0] for n in names}
        strip = len(tops) == 1 and all("/" in n for n in names)
        for info in z.infolist():
            if info.is_dir():
                continue
            rel = info.filename.split("/", 1)[1] if strip else info.filename
            if rel.split("/", 1)[0] in drop:
                continue
            out = dest / rel
            out.parent.mkdir(parents=True, exist_ok=True)
            with z.open(info) as src, out.open("wb") as f:
                shutil.copyfileobj(src, f)


def tracked_files() -> list[str]:
    try:
        out = subprocess.run(["git", "ls-files", "-z"], cwd=ROOT, check=True, capture_output=True).stdout
        return [p for p in out.decode("utf-8").split("\0") if p]
    except (OSError, subprocess.CalledProcessError):
        return [str(p.relative_to(ROOT)).replace(os.sep, "/") for p in ROOT.rglob("*") if p.is_file()]


def copy_source(out: Path) -> int:
    src = out / "source"
    if src.exists():
        shutil.rmtree(src)
    count = 0
    for rel in tracked_files():
        if not any(rel == p or (p.endswith("/") and rel.startswith(p)) for p in SOURCE_PATHS):
            continue
        dst = src / rel
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(ROOT / rel, dst)
        count += 1
    build_id = "unknown"
    try:
        build_id = subprocess.run(["git", "rev-parse", "--short=12", "HEAD"], cwd=ROOT, check=True,
                                  capture_output=True, text=True).stdout.strip()
        dirty = subprocess.run(["git", "diff", "--quiet"], cwd=ROOT).returncode != 0
        if dirty:
            build_id += "-dirty"
    except (OSError, subprocess.CalledProcessError):
        pass
    (src / "BUILD_ID").write_text(build_id + "\n", encoding="utf-8")
    return count


def build_launcher(out: Path, given: str | None) -> Path:
    exe = out / "CTR Launcher.exe"
    if given:
        shutil.copy2(given, exe)
        return exe
    here = ROOT / "launcher"
    bun = shutil.which("bun")
    if not bun or not shutil.which("cargo"):
        raise SystemExit("the launcher needs Bun (bun.sh) and Rust (rustup.rs) to build, or pass --launcher-exe")
    cmd = [bun, "x", "tauri", "build", "--no-bundle"]
    built = here / "src-tauri" / "target" / "release" / "ctr-launcher.exe"
    if os.name != "nt":
        target = "x86_64-pc-windows-gnu"
        cmd += ["--target", target]
        built = here / "src-tauri" / "target" / target / "release" / "ctr-launcher.exe"
    subprocess.run([bun, "install", "--frozen-lockfile"], cwd=here, check=True)
    subprocess.run([bun, "run", "icons"], cwd=here, check=True)
    subprocess.run(cmd, cwd=here, check=True)
    if not built.exists():
        raise SystemExit(f"the launcher build left no {built}")
    shutil.copy2(built, exe)
    # A windows-gnu build loads WebView2 through this DLL; an MSVC build has it linked in.
    loader = built.with_name("WebView2Loader.dll")
    stale = out / loader.name
    if loader.exists():
        shutil.copy2(loader, stale)
    elif stale.exists():
        stale.unlink()
    return exe


README = """CTR Launcher
============

1. Start "CTR Launcher.exe".
2. Drop the folder with your own Crash Team Racing disc image on the window
   (or pick Choose Disc). It must be the NTSC-U disc (SCUS-94426) ripped as
   BIN/CUE (raw 2352-byte sectors).
3. Pick Play.

The menus work like the game's: the arrow keys or a pad's D-pad move, Enter or
Cross/A picks, Esc or Circle/B goes back, and the mouse points and clicks.

The first Play recompiles the game from source/ with the compiler in tools/
(a few minutes; the bar shows each step), then starts it. Later Plays only
rebuild what changed. Rebuild compiles everything again from scratch.

Settings has three tabs (left and right on the top row, or PgUp/PgDn, L1/R1):
  Display   resolution (1280x800 to 2560x1440), fullscreen, widescreen, VSync,
            pixel-perfect scaling
  Graphics  upscaling (sharp pixels, smooth or AMD FSR 1 with its sharpness),
            anti-aliasing (FXAA), CRT effect (scanlines or a curved CRT),
            brightness, texture filtering
  Controls  keyboard, controller, Xbox pads, PlayStation pads, vibration
They are saved in ctr_native.ini as you change them and used the next time you
press Play.

Your disc file is never changed: it is linked (or, on another drive, copied)
into assets\\ctr-u.bin. Logs are in logs\\. Nothing here needs installing.

Cover: click the cover or "Add cover" (or drop a picture on the window) to
use your own art. None is included.

The window is drawn by Microsoft Edge WebView2, which comes with Windows 10
and 11. On an older or stripped-down Windows, install the WebView2 Runtime from
Microsoft first.

Included tools and their licenses: tools\\llvm-mingw (Apache-2.0 with LLVM
exception, mingw-w64 headers and libraries), tools\\cmake (BSD-3-Clause),
tools\\ninja (Apache-2.0). Game source: see source\\LICENSE and
source\\THIRD_PARTY_NOTICES.md. Crash Team Racing is a trademark of Sony
Interactive Entertainment; no game data is included.
"""


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", default=str(ROOT / "dist" / "CTR-Launcher"))
    ap.add_argument("--cache", default=str(ROOT / "build-package" / "cache"))
    ap.add_argument("--llvm-mingw", default=LLVM_MINGW, help="llvm-mingw release tag")
    ap.add_argument("--cmake", default=CMAKE)
    ap.add_argument("--ninja", default=NINJA)
    ap.add_argument("--launcher-exe", help="use this launcher build instead of compiling one")
    ap.add_argument("--no-tools", action="store_true", help="leave tools/ out (for testing the layout)")
    ap.add_argument("--offline", action="store_true", help="only use downloads already in --cache")
    ap.add_argument("--zip", action="store_true", help="also write <out>.zip")
    args = ap.parse_args()

    out = Path(args.out).resolve()
    out.mkdir(parents=True, exist_ok=True)
    tools = out / "tools"

    if not args.no_tools:
        lock = load_lock()
        urls = tool_urls(args)
        try:
            archives = {name: fetch(url, Path(args.cache), lock, args.offline) for name, url in urls.items()}
        finally:
            LOCK.write_text(json.dumps(lock, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        unzip_into(archives["llvm-mingw"], tools / "llvm-mingw", LLVM_MINGW_DROP)
        unzip_into(archives["cmake"], tools / "cmake", CMAKE_DROP)
        unzip_into(archives["ninja"], tools / "ninja", [])
        for need in ["llvm-mingw/bin/i686-w64-mingw32-clang.exe", "cmake/bin/cmake.exe", "ninja/ninja.exe"]:
            if not (tools / need).exists():
                raise SystemExit(f"the tools are missing {need}: check the versions")

    n = copy_source(out)
    print(f"source: {n} files")
    exe = build_launcher(out, args.launcher_exe)
    print(f"launcher: {exe.name}")
    (out / "README.txt").write_text(README.replace("\n", "\r\n"), encoding="utf-8")

    if args.zip:
        archive = out.with_suffix(".zip")
        with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
            for path in sorted(out.rglob("*")):
                rel = path.relative_to(out)
                # A folder someone played from holds their disc and builds: never ship those.
                if path.is_dir() or rel.parts[0] in RUN_ONLY:
                    continue
                z.write(path, Path(out.name) / rel)
        print(f"zip: {archive}")
    for stale in RUN_ONLY:
        if (out / stale).exists():
            print(f"note: {stale} in {out} is from a run; it is not in the zip")
    print(f"done: {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
