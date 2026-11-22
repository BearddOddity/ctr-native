#!/usr/bin/env python3
"""Make the CTR Launcher release folder: everything a player needs, nothing to install.

    python tools/launcher/package_windows.py [--out dist/CTR-Launcher] [--cover my-cover.png] [--zip]

The folder it writes:

    CTR Launcher.exe     the launcher (launcher/: Tauri, Bun and Rust; its menus look like the game's)
    source/              the game's source (what Play recompiles), with its build recipe
    compiler/            the compiler made for this port: clang, lld and llvm-ar from llvm-mingw, with only
                         the headers and libraries the game's build reads (tools/launcher/recipe.py)
    webview2/            Microsoft's Edge WebView2 runtime (its Fixed Version, x64), which draws the window on a
                         PC whose Windows has none (--webview2; Windows' own copy is used when there is one)
    cover.png            only with --cover: your own picture of the cover, shown until another is picked
    README.txt

The player drops their own CTR disc (NTSC-U .bin/.cue or raw .iso) on the launcher and presses
Play: it links the disc into assets/, recompiles the game into build/ by running
source/tools/launcher/build-recipe.json with compiler/ (no CMake, no Ninja) and
starts it. No game data is ever put in the package, and no cover unless you name
your own with --cover (it is never committed to git).

How compiler/ is made: llvm-mingw for Windows, and off Windows one for this machine
too, are downloaded once into --cache and checked against
tools/launcher/toolchain.lock.json (sha256; an archive with no entry yet has its
hash written in on first download: commit the file afterwards; a mismatch stops).
The packaged source is built from the recipe with the whole llvm-mingw, noting
every file the build reads; compiler/ gets those files and the programs and DLLs
that read them. Then the source is built again from scratch with only compiler/
(off Windows, with the same files cut out of this machine's llvm-mingw): a file it
lacks stops the packaging.
The launcher is built from launcher/ with Bun and Cargo (`bun install`, then
`tauri build`): run it on Windows with both on PATH, or pass a launcher built some
other way with --launcher-exe. Off Windows it cross-builds for x86_64-pc-windows-gnu
(rustup target add x86_64-pc-windows-gnu, and mingw-w64), which also needs
WebView2Loader.dll beside the exe; it is copied in.

The window is Microsoft Edge WebView2. Windows 11 has it and so do most Windows 10
PCs, but not all (LTSC, PCs kept offline), so the package carries Microsoft's
Fixed Version runtime: download it from the WebView2 page (Fixed Version, x64)
and pass the .cab with --webview2 (or a link to it, fetched and pinned in the lock
like llvm-mingw, or the folder it unpacks to). A package made once keeps its
webview2/ for later runs. --no-webview2 makes one that needs WebView2 on the PC.

Last, every program in the package (the launcher, compiler/, and the game it just
built) is read for the DLLs it loads: one that is neither Windows' own nor in the
package stops the packaging, so nothing but the player's disc is ever needed.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import shutil
import subprocess
import sys
import tarfile
import urllib.request
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import recipe  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
LOCK = Path(__file__).with_name("toolchain.lock.json")

# The pinned llvm-mingw release the compiler is cut from (tools/launcher/build-recipe.json is written with it).
LLVM_MINGW = "20241217"

# What Play compiles: the game, its platform layer and SDL (the launcher is built here, not by Play).
SOURCE_PATHS = [
    "CMakeLists.txt",
    "main.c",
    "LICENSE",
    "THIRD_PARTY_NOTICES.md",
    "README.md",
    "tools/launcher/build-recipe.json",
    "game/",
    "include/",
    "platform/",
    "externals/SDL/",
]

# Microsoft's Edge WebView2, which draws the window (launcher/src-tauri/src/webview.rs): its Fixed Version runtime.
WEBVIEW2_PAGE = "https://developer.microsoft.com/microsoft-edge/webview2/ (Fixed Version, x64)"

# DLLs every 64-bit Windows 10 and 11 has. A program in the package may load these, its own neighbours in the package,
# and Windows' API sets (api-ms-win-*, ext-ms-win-*). Anything else (vcruntime140.dll, msvcp140.dll, a mingw
# runtime DLL, WebView2Loader.dll left out) would be a dependency the player has to install.
WINDOWS_DLLS = {
    "advapi32.dll", "avrt.dll", "bcrypt.dll", "bcryptprimitives.dll", "cfgmgr32.dll", "combase.dll", "comctl32.dll",
    "comdlg32.dll", "credui.dll", "crypt32.dll", "cryptbase.dll", "d2d1.dll", "d3d11.dll", "d3d12.dll", "d3d9.dll",
    "d3dcompiler_47.dll", "dbghelp.dll", "dcomp.dll", "dinput8.dll", "dnsapi.dll", "dsound.dll", "dwmapi.dll",
    "dwrite.dll", "dxgi.dll", "gdi32.dll", "gdiplus.dll", "glu32.dll", "hid.dll", "imagehlp.dll", "imm32.dll",
    "iphlpapi.dll", "kernel32.dll", "kernelbase.dll", "ksuser.dll", "mf.dll", "mfplat.dll", "mfreadwrite.dll",
    "mpr.dll", "msimg32.dll", "msvcp_win.dll", "msvcrt.dll", "mswsock.dll", "ncrypt.dll", "netapi32.dll",
    "normaliz.dll", "ntdll.dll", "ole32.dll", "oleacc.dll", "oleaut32.dll", "opengl32.dll", "powrprof.dll",
    "propsys.dll", "psapi.dll", "rpcrt4.dll", "secur32.dll", "setupapi.dll", "shcore.dll", "shell32.dll",
    "shlwapi.dll", "sspicli.dll", "ucrtbase.dll", "uiautomationcore.dll", "urlmon.dll", "user32.dll", "userenv.dll",
    "uxtheme.dll", "version.dll", "winhttp.dll", "wininet.dll", "winmm.dll", "winspool.drv", "wintrust.dll",
    "wldap32.dll", "ws2_32.dll", "wsock32.dll", "wtsapi32.dll", "xinput1_4.dll", "xinput9_1_0.dll",
    "windowscodecs.dll", "dxcore.dll", "coremessaging.dll", "sechost.dll", "winusb.dll", "wevtapi.dll", "pdh.dll",
}

# Made by playing from the folder (the player's disc, builds, logs, settings): never packaged.
RUN_ONLY = ["assets", "build", "logs", "memcards", "ctr_native.ini", "launcher.ini"]


def tool_urls(version: str) -> dict[str, str]:
    """llvm-mingw for Windows (what compiler/ is cut from) and, off Windows, one that runs here."""
    base = f"https://github.com/mstorsjo/llvm-mingw/releases/download/{version}/llvm-mingw-{version}-ucrt"
    urls = {"windows": f"{base}-x86_64.zip"}
    if os.name != "nt":
        if sys.platform == "darwin":
            urls["host"] = f"{base}-macos-universal.tar.xz"
        elif platform.machine().lower() in ("x86_64", "amd64"):
            urls["host"] = f"{base}-ubuntu-20.04-x86_64.tar.xz"
        elif platform.machine().lower() in ("aarch64", "arm64"):
            urls["host"] = f"{base}-ubuntu-20.04-aarch64.tar.xz"
        else:
            raise SystemExit(f"no llvm-mingw release runs on this {platform.machine()}: package on Windows")
    return urls


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


def unpack(archive: Path, dest: Path) -> Path:
    """A whole llvm-mingw archive, unpacked once next to it; its one top folder."""
    done = dest / ".unpacked"
    if not done.exists():
        if dest.exists():
            shutil.rmtree(dest)
        dest.mkdir(parents=True)
        print(f"unpacking {archive.name}")
        if archive.suffix == ".zip":
            with zipfile.ZipFile(archive) as z:
                z.extractall(dest)
        else:
            with tarfile.open(archive) as t:
                t.extractall(dest, filter="tar") if hasattr(tarfile, "data_filter") else t.extractall(dest)
        done.write_text(archive.name + "\n", encoding="utf-8")
    tops = [p for p in dest.iterdir() if p.is_dir()]
    if len(tops) != 1:
        raise SystemExit(f"{archive.name} should hold one folder")
    return tops[0]


def make_compiler(out: Path, cache: Path, work: Path, version: str, offline: bool) -> None:
    """compiler/: llvm-mingw cut down to what this port's build reads, proven by building the game with it."""
    lock = load_lock()
    try:
        archives = {k: fetch(u, cache, lock, offline) for k, u in tool_urls(version).items()}
    finally:
        LOCK.write_text(json.dumps(lock, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    windows = unpack(archives["windows"], cache / f"llvm-mingw-{version}-windows")
    host = unpack(archives["host"], cache / f"llvm-mingw-{version}-host") if "host" in archives else windows
    plan = recipe.load(out / "source" / "tools" / "launcher" / "build-recipe.json")
    src = out / "source"

    print("measuring: building the game once with the whole llvm-mingw")
    used_file = work / "used.json"
    if not recipe.run_recipe(plan, host, src, work / "measure", used=used_file, log=quiet_log(work / "measure.log")):
        raise SystemExit(f"the game didn't build with the whole llvm-mingw: see {work / 'measure.log'}")
    used = json.loads(used_file.read_text(encoding="utf-8"))
    about = recipe.make_compiler(windows, used, out / "compiler", plan, windows=True)
    print(f"compiler: {about['files']} files, {about['bytes'] / 1e6:.1f} MB")

    print("proving it: building the game from scratch with only those files")
    shutil.rmtree(work / "verify", ignore_errors=True)
    proof = out / "compiler" if host == windows else work / "compiler-here"
    if proof != out / "compiler":
        recipe.make_compiler(host, used, proof, plan, windows=False)
    if not recipe.run_recipe(plan, proof, src, work / "verify", log=quiet_log(work / "verify.log")):
        raise SystemExit(f"the cut compiler is missing something: see {work / 'verify.log'}")
    print("  the game builds with it")


def quiet_log(path: Path):
    """A recipe run's log in a file, and one line in ten on the screen."""
    path.parent.mkdir(parents=True, exist_ok=True)
    f = path.open("w", encoding="utf-8")
    seen = [0]

    def log(line: str) -> None:
        f.write(line + "\n")
        f.flush()
        if line.startswith("["):
            seen[0] += 1
            if seen[0] % 50 == 0:
                print(f"  {line}")

    return log


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


def unpack_cab(cab: Path, dest: Path) -> None:
    """A .cab, unpacked once: with Windows' own expand.exe (as Microsoft's WebView2 steps say), else cabextract."""
    done = dest / ".unpacked"
    if done.exists():
        return
    if dest.exists():
        shutil.rmtree(dest)
    dest.mkdir(parents=True)
    print(f"unpacking {cab.name}")
    if os.name == "nt":
        cmd = ["expand", str(cab), "-F:*", str(dest)]
    elif shutil.which("cabextract"):
        cmd = ["cabextract", "-q", "-d", str(dest), str(cab)]
    else:
        raise SystemExit("--webview2: a .cab needs cabextract off Windows (or pass the folder it unpacks to)")
    subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL)
    done.write_text(cab.name + "\n", encoding="utf-8")


def runtime_folder(root: Path) -> Path | None:
    """The folder holding msedgewebview2.exe: `root` itself or the one folder in it that does (as launcher's
    webview.rs looks)."""
    if (root / "msedgewebview2.exe").is_file():
        return root
    dirs = [d for d in root.iterdir() if d.is_dir()] if root.is_dir() else []
    return dirs[0] if len(dirs) == 1 and (dirs[0] / "msedgewebview2.exe").is_file() else None


def place_webview2(out: Path, given: str | None, cache: Path, offline: bool) -> str | None:
    """webview2/: Microsoft's WebView2 Fixed Version runtime from --webview2 (a .cab, its link, a .zip of it or the
    folder it unpacks to), or the one an earlier run put there. Returns the version, or None with --no-webview2."""
    dest = out / "webview2"
    if not given:
        if runtime_folder(dest) == dest:
            return (dest / ".version").read_text(encoding="utf-8").strip() if (dest / ".version").exists() else "?"
        raise SystemExit(
            "The package needs Microsoft's WebView2 runtime for PCs without one (some Windows 10 PCs). Download it "
            f"from {WEBVIEW2_PAGE} and pass the .cab with --webview2, or --no-webview2 for a package that needs "
            "WebView2 on the player's PC.")
    if given.startswith("https://"):
        lock = load_lock()
        try:
            source = fetch(given, cache, lock, offline)
        finally:
            LOCK.write_text(json.dumps(lock, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    else:
        source = Path(given)
        if not source.exists():
            raise SystemExit(f"--webview2: no such file or folder: {source}")
    if source.is_dir():
        unpacked = source
    elif source.suffix.lower() == ".cab":
        unpacked = cache / "webview2" / source.stem
        unpack_cab(source, unpacked)
    elif source.suffix.lower() == ".zip":
        unpacked = cache / "webview2" / source.stem
        if not (unpacked / ".unpacked").exists():
            shutil.rmtree(unpacked, ignore_errors=True)
            with zipfile.ZipFile(source) as z:
                z.extractall(unpacked)
            (unpacked / ".unpacked").write_text(source.name + "\n", encoding="utf-8")
    else:
        raise SystemExit("--webview2: give the Fixed Version .cab, a .zip of it, or the folder it unpacks to")
    folder = runtime_folder(unpacked)
    if folder is None:
        raise SystemExit(f"--webview2: no msedgewebview2.exe in {unpacked}: is it the WebView2 Fixed Version runtime?")
    machine = recipe.pe_machine(folder / "msedgewebview2.exe")
    if machine != 0x8664:
        raise SystemExit(f"--webview2: that runtime is for machine {machine:#x}; the launcher is 64-bit x86: use the "
                         "x64 Fixed Version")
    version = folder.name.removeprefix("Microsoft.WebView2.FixedVersionRuntime.").removesuffix(".x64")
    if dest.exists():
        shutil.rmtree(dest)
    shutil.copytree(folder, dest, ignore=shutil.ignore_patterns(".unpacked"))
    (dest / ".version").write_text(version + "\n", encoding="utf-8")
    return version


def foreign_dlls(program: Path, beside: set[str]) -> list[str]:
    """The DLLs a program loads that are neither Windows' own nor among `beside` (the package's, lower case)."""
    out = []
    for dll in recipe.pe_imports(program):
        low = dll.lower()
        if low in WINDOWS_DLLS or low in beside or low.startswith(("api-ms-win-", "ext-ms-win-")):
            continue
        out.append(dll)
    return out


def audit(out: Path, game: Path | None) -> list[str]:
    """Every program a player runs from the package, and the game it builds: what each loads from outside."""
    programs = [out / "CTR Launcher.exe"] + sorted((out / "compiler").rglob("*.exe")) + \
        sorted((out / "compiler").rglob("*.dll"))
    if game is not None and game.is_file():
        programs.append(game)
    problems = []
    for prog in programs:
        if not prog.is_file():
            continue
        beside = {p.name.lower() for p in prog.parent.iterdir() if p.suffix.lower() == ".dll"}
        for dll in foreign_dlls(prog, beside):
            problems.append(f"{prog.name} loads {dll}, which Windows doesn't have and the package doesn't carry")
    return problems


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
2. Drop your own Crash Team Racing disc image on the window: the .bin, its
   .cue, an .iso or the folder holding it (or pick Choose Disc). It must be the
   NTSC-U disc (SCUS-94426) copied with raw 2352-byte sectors, as BIN/CUE or
   an .iso of that kind. A cooked .iso (2048-byte sectors) lacks the music,
   voice and video sectors, and the launcher says so.
3. Pick Play.

The menus work like the game's: the arrow keys or a pad's D-pad move, Enter or
Cross/A picks, Esc or Circle/B goes back, and the mouse points and clicks.

The first Play recompiles the game from source/ with compiler/, the compiler
made for this port (a minute or two; the bar shows each step), then starts it.
Later Plays only rebuild what changed. Rebuild compiles everything again from
scratch.

Settings has three tabs (left and right on the top row, or PgUp/PgDn, L1/R1):
  Display   resolution (1280x800 to 2560x1440), fullscreen, widescreen, VSync,
            pixel-perfect scaling
  Graphics  internal resolution (1x, the PlayStation's own, to 8x),
            upscaling (sharp pixels, smooth or AMD FSR 1 with its sharpness),
            anti-aliasing (FXAA), CRT effect (scanlines or a curved CRT),
            brightness, texture filtering
  Controls  keyboard, controller, Xbox pads, PlayStation pads, vibration
They are saved in ctr_native.ini as you change them and used the next time you
press Play.

Your disc file is never changed: it is linked (or, on another drive, copied)
into assets\\ctr-u.bin. Logs are in logs\\.

{cover}

{webview2}

compiler\\ is cut from llvm-mingw: clang, lld and llvm-ar (Apache-2.0 with LLVM
exception, see compiler\\LICENSE.TXT) with only the mingw-w64 headers and
libraries the game's build reads. Game source: see source\\LICENSE and
source\\THIRD_PARTY_NOTICES.md. Crash Team Racing is a trademark of Sony
Interactive Entertainment; no game data is included.
"""


WEBVIEW2_GIVEN = """Nothing here needs installing: all it needs is 64-bit Windows 10 or 11 and your
own disc. The window is drawn by Microsoft Edge WebView2. Windows 11 and most
Windows 10 PCs have it; for the rest, webview2\\ holds Microsoft's own copy (the
WebView2 Fixed Version runtime {version}), which the launcher uses when Windows has
none. To use it always, put this in launcher.ini:
  [window]
  webview2 = bundled"""

WEBVIEW2_NONE = """The window is drawn by Microsoft Edge WebView2, which Windows 11 and most
Windows 10 PCs have. This package doesn't carry its own copy: on a PC without
it, install the WebView2 Runtime from Microsoft first."""

COVER_NAMES = ["cover.png", "cover.jpg", "cover.jpeg", "cover.webp", "cover.bmp", "cover.gif"]

COVER_NONE = """Cover: click the cover or "Add cover" (or drop a picture on the window) to
use your own art. None is included."""

COVER_GIVEN = """Cover: {name} beside the launcher is the cover it shows, put in by whoever made
this package. Click the cover or "Change cover" (or drop a picture on the window)
to use another."""


def picture_ext(head: bytes) -> str | None:
    """A picture's type from its first bytes, as the launcher reads it (launcher/src-tauri/src/cover.rs)."""
    if head.startswith(b"\x89PNG\r\n\x1a\n"):
        return ".png"
    if head.startswith(b"\xff\xd8\xff"):
        return ".jpg"
    if head.startswith((b"GIF87a", b"GIF89a")):
        return ".gif"
    if head.startswith(b"BM"):
        return ".bmp"
    if len(head) >= 12 and head[:4] == b"RIFF" and head[8:12] == b"WEBP":
        return ".webp"
    return None


def place_cover(out: Path, given: str | None) -> Path | None:
    """--cover: the person's own picture beside the launcher as cover.<type>, which it shows (main.rs `load`)
    until another is picked. Without --cover, a cover already in the folder is left as it is."""
    if given:
        src = Path(given)
        if not src.is_file():
            raise SystemExit(f"--cover: no such file: {src}")
        if src.stat().st_size > 40 << 20:
            raise SystemExit("--cover: the picture is over 40 MB, which the launcher refuses")
        with open(src, "rb") as f:
            ext = picture_ext(f.read(16))
        if not ext:
            raise SystemExit("--cover: use a PNG, JPEG, BMP, GIF or WebP picture")
        for name in COVER_NAMES:
            (out / name).unlink(missing_ok=True)
        shutil.copyfile(src, out / f"cover{ext}")
    return next((out / n for n in COVER_NAMES if (out / n).is_file()), None)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", default=str(ROOT / "dist" / "CTR-Launcher"))
    ap.add_argument("--cache", default=str(ROOT / "build-package" / "cache"))
    ap.add_argument("--llvm-mingw", default=LLVM_MINGW, help="llvm-mingw release tag")
    ap.add_argument("--launcher-exe", help="use this launcher build instead of compiling one")
    ap.add_argument("--cover", help="your own picture of the game's cover, put in the package as cover.png (never in git)")
    ap.add_argument("--webview2", help="Microsoft's WebView2 Fixed Version runtime (x64): its .cab, a link to it, a "
                    ".zip of it or the folder it unpacks to; put in the package as webview2/")
    ap.add_argument("--no-webview2", action="store_true", help="no webview2/: the package then needs WebView2 on the PC")
    ap.add_argument("--no-compiler", action="store_true", help="leave compiler/ out (for testing the layout)")
    ap.add_argument("--offline", action="store_true", help="only use downloads already in --cache")
    ap.add_argument("--zip", action="store_true", help="also write <out>.zip")
    args = ap.parse_args()

    out = Path(args.out).resolve()
    out.mkdir(parents=True, exist_ok=True)
    if (out / "tools").exists():
        shutil.rmtree(out / "tools")  # an older package's llvm-mingw, CMake and Ninja

    cache, work = Path(args.cache), Path(args.cache).parent / "work"
    if args.no_webview2:
        shutil.rmtree(out / "webview2", ignore_errors=True)
        webview2 = None
    else:
        webview2 = place_webview2(out, args.webview2, cache, args.offline)  # first: it may stop before the builds
    print(f"webview2: {'Fixed Version ' + webview2 if webview2 else 'none (the PC must have WebView2)'}")
    n = copy_source(out)
    print(f"source: {n} files")
    if not args.no_compiler:
        make_compiler(out, cache, work, args.llvm_mingw, args.offline)
    exe = build_launcher(out, args.launcher_exe)
    print(f"launcher: {exe.name}")
    cover = place_cover(out, args.cover)
    print(f"cover: {cover.name if cover else 'none'}")
    note = COVER_GIVEN.format(name=cover.name) if cover else COVER_NONE
    web = WEBVIEW2_GIVEN.format(version=webview2) if webview2 else WEBVIEW2_NONE
    (out / "README.txt").write_text(README.format(cover=note, webview2=web).replace("\n", "\r\n"), encoding="utf-8")

    problems = audit(out, None if args.no_compiler else work / "verify" / "ctr_native.exe")
    if problems:
        raise SystemExit("the package would need more than the player's disc:\n  " + "\n  ".join(problems))
    print("needs: nothing but 64-bit Windows 10 or 11 and the player's disc" if webview2 else
          "needs: 64-bit Windows 10 or 11 with WebView2, and the player's disc")

    if args.zip:
        archive = out.with_suffix(".zip")
        with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
            for path in sorted(out.rglob("*")):
                rel = path.relative_to(out)
                # A folder someone played from holds their disc and builds: never ship those.
                if path.is_dir() or rel.parts[0] in RUN_ONLY or path.name == ".ctr-launcher-acl":
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
