# CTR Native

A native PC port of Crash Team Racing (PS1, 1999), built on top of the [CTR-ModSDK](https://github.com/CTR-tools/CTR-ModSDK) decompilation project.

## Philosophy

- **No byte budget.** Game source lives in `game/` as our own copies. Edit freely.
- **No PSX toolchain.** Targets Windows and Linux with SDL3. No MIPS compiler needed.
- **Clean platform layer.** `main.c` owns process startup; host details stay in `platform/native_*`.
- **No build system nonsense.** Just `build.bat` / `build.sh`.
- **Fully static build.** Single executable, zero dependencies. SDL3 is compiled from vendored source and linked statically.

## Directory Layout

```
ctr_native/
  main.c              Entrypoint and native platform boundary
  platform/           Native-owned audio, input, memcard, CD, and PSX facade glue
  build-msvc.bat      Windows build (MSVC x86)
  build.bat           Windows build (MinGW i686)
  build.sh            Linux build
  CMakePresets.json   Shared CLion/command-line CMake configurations
  README.md           This file
  game/               Our copies of all decompiled game source (943 files)
    game_unity.h      Ordered unity include chain for all game source files
  include/            Project headers (structs, globals, declarations, platform facade)
  externals/
    SDL/              SDL3 source (static build)
```

## Prerequisites

### Windows

The recommended native Windows toolchain is MSVC x86:

1. Install Visual Studio 2022 or Visual Studio Build Tools 2022.
2. Select the **Desktop development with C++** workload and a current Windows SDK.
3. Ensure CMake 3.20 or newer is on `PATH` (standalone or the Visual Studio C++ CMake tools component).
4. Run `build-msvc.bat`, or select the `windows-msvc-x86` CMake preset in CLion.

The existing MinGW i686 build remains supported:

1. Install [MSYS2](https://www.msys2.org/).
2. In an MSYS2 terminal:
   ```
   pacman -Syu
   pacman -S --needed git mingw-w64-i686-gcc mingw-w64-i686-cmake mingw-w64-i686-make
   ```
   If the update asks you to close the terminal, reopen MSYS2 and run the install command.
3. Add `C:\msys64\mingw32\bin` to your system PATH
4. Open a new Command Prompt or PowerShell and run `build.bat`.

That's it. SDL3 is compiled from vendored source -- no separate install needed.

### Linux (Debian/Ubuntu)

```
sudo dpkg --add-architecture i386 && sudo apt update
sudo apt install gcc-multilib
sudo apt install libx11-dev libxext-dev libgl1-mesa-dev libasound2-dev libudev-dev libdbus-1-dev
sudo apt install libx11-dev:i386 libxext-dev:i386 libxcursor-dev:i386 libxfixes-dev:i386 libxi-dev:i386 libxrandr-dev:i386
```

The build is 32-bit, so SDL needs the `:i386` X11 packages to open a window (without them, or Wayland's, CMake stops
with "SDL could not find X11 or Wayland development libraries").

## Building

```
build-msvc.bat       # Windows, MSVC x86 (recommended)
build.bat            # Windows, MinGW i686
chmod +x build.sh
./build.sh           # Linux
```

The shared CMake presets can also be used directly or selected as CLion CMake profiles:

```
cmake --preset windows-msvc-x86
cmake --build --preset windows-msvc-x86-debug
ctest --preset windows-msvc-x86-debug
```

First build compiles SDL3 from source. This is cached as a static library in the selected build directory.

Output:

- MSVC: `build-msvc-x86/Release/ctr_native.exe`
- MinGW: `build/ctr_native.exe`
- Linux: `build/ctr_native`

### Clean build

```
rmdir /s /q build    # Windows: delete cached libraries
build.bat            # Windows: rebuild everything

rmdir /s /q build-msvc-x86
build-msvc.bat       # Windows MSVC: rebuild everything

rm -rf build/        # Linux: delete cached libraries
./build.sh           # Linux: rebuild everything
```

## Running

### Normal Setup

If you downloaded a release build, you only need two things for normal play:

1. The game executable:
   - `ctr_native.exe` on Windows
   - `ctr_native` on Linux
2. Your own NTSC-U retail CTR disc image, named (put in directory called `assets`):
   - `assets/ctr-u.bin`

Example:

```
CTR-Native/
  ctr_native.exe
  assets/
    ctr-u.bin
```

Then run `ctr_native.exe`.

The disc image must be the common single-track raw PSX BIN layout: MODE2/2352 sectors, with the data track starting at byte 0. A cooked 2048-byte `.iso` does not preserve the XA/STR sector data needed for audio and video playback.

For development builds run from `build/`, put the same `assets/ctr-u.bin` next to the source tree:

```
ctr-native/
  build/
    ctr_native.exe
  assets/
    ctr-u.bin
```

### CTR Launcher (Windows)

`CTR Launcher.exe` is a player front end in the game's own menu style: boxes with
the game's edge colour and drop shadow, its orange lettering, the pulsing row
highlight and a waving checkered flag behind them (drawn from `game/RECTMENU.c`
and the game's colour table; no game art, font or logo). The left side shows a
cover picture the player picks (none is shipped). The right side takes the
player's own disc image (drop the `.bin`, its `.cue`, an `.iso` or the folder
on the window, or pick Choose Disc; the launcher judges the file by its sectors,
not its name: raw 2352-byte sectors play, a cooked 2048-byte `.iso` is refused
with the reason, since it lacks the music, voice and video sectors) and has
Play, which recompiles the game and starts
it with a progress bar per build step. Settings has three tabs: Display,
Graphics and Controls. Arrows, Enter and Esc, a pad, or the mouse drive it.

It is a Tauri app in `launcher/`: the window is TypeScript built with Bun
(`launcher/src`), the work (disc check, settings, the build, starting the game)
is Rust (`launcher/src-tauri`). Windows 10 and 11 ship the WebView2 runtime it
draws with.

```
cd launcher
bun install
bun run dev        # the window in a browser, with a stand-in for the Rust side
bun test           # the menus and the progress bar
bun run app        # the real app (tauri dev)
bun run build      # src-tauri/target/release/ctr-launcher.exe
cd src-tauri && cargo test
```

Run from a checkout, it builds into `build-launcher/` with CMake and keeps its
settings beside the checkout. Make the self-contained package (the launcher,
this source under `source/` and its own compiler under `compiler/`, nothing to
install for the player):

```
python tools/launcher/package_windows.py --zip
```

The package carries no CMake and no Ninja. Play runs
`tools/launcher/build-recipe.json`: every compile, the SDL archive and the link,
written from `CMakeLists.txt` by `tools/launcher/recipe.py generate` with the
`{src}`, `{build}` and `{build_id}` of the machine left as placeholders. The
launcher runs its steps several at once and skips a step whose command and files
didn't change since it last ran.

`compiler/` is made for this port. The packager builds the packaged source once
from the recipe with the whole llvm-mingw (the version pinned in the script),
notes every header and library the build reads, and copies only those, plus
clang, lld, llvm-ar and the DLLs they load, out of llvm-mingw's Windows release:
about 160 MB (50 MB zipped) where llvm-mingw, CMake and Ninja were over 700 MB.
It then builds the game again from scratch with only that, so a missing file
stops the packaging rather than a player's first Play. Each llvm-mingw archive's
sha256 is pinned in `tools/launcher/toolchain.lock.json`; a mismatch stops.
Off Windows the packager also takes llvm-mingw for that machine to run the
builds.

The window is Microsoft Edge WebView2. Windows 11 has it and so do most Windows
10 PCs, but not all (LTSC, PCs kept offline), so the package carries Microsoft's
WebView2 Fixed Version runtime in `webview2/`: download it from
<https://developer.microsoft.com/microsoft-edge/webview2/> (Fixed Version, x64)
and pass the `.cab` with `--webview2` (a later run keeps it). The launcher uses
Windows' own copy when there is one (Windows Update keeps it current) and the
bundled one otherwise, or always with `webview2 = bundled` under `[window]` in
`launcher.ini`; `logs/launcher.log` says which. If neither is there it says why
in a Windows message box instead of closing silently. `--no-webview2` makes a
package that needs WebView2 on the PC. Last, the packager reads every program in
the package and the game it just built for the DLLs they load, and stops on any
that is neither Windows' own nor in the package (a Visual C++ runtime, say), so
the player needs nothing but 64-bit Windows 10 or 11 and their own disc.

`--no-compiler` leaves `compiler/` out; `--launcher-exe` takes a
launcher built elsewhere. The zip never holds a disc image, a build, saves or
settings. `--cover my-cover.png` puts your own picture of the cover in the
package as `cover.png`, which the launcher shows until another is picked. Its
slot is 418 x 474 (shown larger on a bigger window: 836 x 948 stays sharp full
screen). The picture is never committed; keep it out of a package you share.

When a change to `CMakeLists.txt` (or a source added or removed) changes the
build, write the recipe again and commit it:

```
python tools/launcher/recipe.py generate --toolchain <llvm-mingw for this machine>
```

`tools/check.py` compares it with `CMakeLists.txt` when `CTR_LLVM_MINGW` names an
llvm-mingw (CI does), with the CMake version the recipe was written with
(`CTR_CMAKE` names another CMake than PATH's).

The disc is never changed: it is hard linked (or, on another drive, copied) to
`assets/ctr-u.bin`, and a different file already there is renamed `.oldN`.
Logs go to `logs/build.log` and `logs/game.log`.

### Game settings (`ctr_native.ini`)

The game reads `ctr_native.ini` beside its data (the launcher writes it), then
`CTR_NATIVE_*` environment variables, then the command line. Hand edits work:

```
[video]
width = 1920
height = 1080
fullscreen = 0
widescreen = 1        ; 0 = 4:3 pillarboxed
vsync = 1
[graphics]
upscale = fsr         ; sharp | smooth | fsr
sharpness = medium    ; low | medium | high (FSR only)
antialiasing = fxaa   ; off | fxaa
crt = off             ; off | scanlines | curved
brightness = 0        ; -2 .. 2
texture_filter = 0    ; bilinear filtering in 3D
integer_scale = 0     ; whole-number scaling only
[input]
keyboard = 1
controller = 1
xbox = 1
playstation = 1
vibration = 1
```

Command line: `--config FILE`, `--width N`, `--height N`, `--resolution WxH`,
`--fullscreen`, `--windowed`, `--widescreen`, `--no-widescreen`, `--vsync`,
`--no-vsync`, `--upscale sharp|smooth|fsr`, `--aa off|fxaa`,
`--crt off|scanlines|curved`, `--no-keyboard`, `--no-controller`, `--no-xbox`,
`--no-playstation`. Environment: `CTR_NATIVE_WIDTH`, `_HEIGHT`, `_FULLSCREEN`,
`_ASPECT` (`4:3`, `16:9`, `16:10`), `_KEYBOARD`, `_GAMEPAD`.

### Extracted Asset Override

You do not need extracted assets for normal play.

Extracted files are still supported for development, modding, and debugging. If present, they override files from `ctr-u.bin`.

Extracted-asset override structure:

```
CTR-Native/
  ctr_native.exe
  assets/
    BIGFILE.BIG
    SOUNDS/KART.HWL
    TEST.STR
    XA/
      ENG.XNF
      ENG/EXTRA/S00.XA ... S05.XA
      ENG/GAME/S00.XA ... S20.XA
      MUSIC/S00.XA ... S01.XA
```

The full extracted asset list is:

- `BIGFILE.BIG`
- `SOUNDS/KART.HWL`
- `TEST.STR`
- `XA/ENG.XNF`
- `XA/ENG/EXTRA/S00.XA` through `S05.XA`
- `XA/ENG/GAME/S00.XA` through `S20.XA`
- `XA/MUSIC/S00.XA` through `S01.XA`

`BIGFILE.BIG` and `SOUNDS/KART.HWL` are required. Missing XA/XNF files or
`TEST.STR` produce a startup warning: the game can continue without the affected streamed audio or Scrapbook video. Files available in `ctr-u.bin` still provide the fallback for missing extracted files. A present but invalid XA manifest remains a startup error.

## Bug Replays

Internal builds can record a small bug report folder. See `docs/REPLAYS.md`.

## Architecture

```
main.c (entrypoint)
  |
  +-- platform/native_* (platform shell, audio, input, memcard, CD, renderer, PSX facade glue)
  |
  +-- game/game_unity.h
        |
        +-- game/ (all decompiled game source)
              |
              +-- include/ (headers: structs, globals, declarations)
```

- `CTR_NATIVE` is defined for native host/platform-specific code
- First-party native code targets portable C17 with compiler extensions disabled
- The default build uses 32-bit mode while remaining PSX address-shaped data and host-pointer contracts are audited. GPU primitive links are bridged through 24-bit native tokens; see `docs/MEMORY_MODEL.md`.

## Roadmap

- Clean up `game/` copies strip byte budget hacks and route platform-specific code through `CTR_NATIVE`
- Keep reducing 32-bit host-pointer assumptions in PSX-shaped data, and keep pruning inherited compatibility code now owned in `include/` and `platform/`.

## Credits

- [CTR-ModSDK](https://github.com/CTR-tools/CTR-ModSDK) — the decompilation project this is built on
- [PsyCross](https://github.com/OpenDriver2/PsyCross) — original PS1 compatibility code from which parts of CTR Native's owned platform layer and PsyQ facade headers are derived
- [SDL3](https://github.com/libsdl-org/SDL) — cross-platform multimedia
- Crash Team Racing is a trademark of Sony Computer Entertainment / Naughty Dog
