# Regression Safeguards

One command runs every check this machine can run:

```sh
python3 tools/check.py                         # matching tests, match lock, native build and tests
python3 tools/check.py --sanitize              # plus an AddressSanitizer/UBSan build and its tests
python3 tools/check.py --replays debug/reports # plus every replay you recorded (needs your disc's assets)
```

A step this machine can't run says SKIP with the reason; a skip is never counted as a pass.

On Windows the native builds use clang-cl inside Visual Studio's x86 environment (`vcvars32.bat`, found with vswhere)
with Ninja, or NMake when Ninja isn't there: plain MSVC can't compile the game's GCC statement expressions. clang-cl
comes from LLVM's installer or Visual Studio's "C++ Clang tools for Windows"; `CTR_CLANG_CL` names another one. The
match lock needs the vendored PSX toolchain, which is Linux ELF: on Windows it says SKIP, so run it in WSL from a
checkout in WSL's own file system (`~/ctr-native`; GCC 2.8.1 can't work on `/mnt/<drive>`).

## Function Map

`platform/native_function_map.inc` lists every game function by a hash of its name, so a checkpoint written by one
build restores in another (docs/REPLAYS.md). `tools/gen_function_map.py` writes it from the sources as the compiler
sees them; `--check` (a `tools/check.py` step) fails when a function was added or removed without writing it again.
`ctr_native --checkpoint-selftest` (ctest `ctr_checkpoint_translation`) restores a made-up checkpoint whose regions
and functions all sit 16 MiB away and checks the pointers come back.

## Match Lock

`tools/matching/lock.py` keeps `metadata/retail/ntsc-u-926/match_lock.json`: for every target in `matching.json`,
a fingerprint of each matched function (and of each overlay's sections) as GCC 2.8.1 compiles it today, with every
relocation to a retail symbol applied the way the linker would. It needs the vendored PSX toolchain, not the retail
disc, so it runs anywhere the toolchain is built.

```sh
python3 tools/matching/lock.py check     # names every matched function whose bytes changed
python3 tools/matching/lock.py update    # after a deliberate change you verified with match.sh
```

A function rewritten from `__asm__` into C that compiles to the same linked bytes keeps its lock entry, so the lock
also proves such conversions. An edit that changes a matched function fails `check` with the function's name.

## Git Hook

```sh
git config core.hooksPath tools/hooks
```

Before each commit that touches `game/`, `include/`, `tools/matching/` or `metadata/`, the hook runs the matching unit
tests and the match lock (`tools/check.py --fast`, about 20 s). Skip it once with `git commit --no-verify`.

## Sanitizers

`-DCTR_NATIVE_SANITIZE="address;undefined"` builds `ctr_native` and its tests with AddressSanitizer and UBSan
(GCC/Clang; LeakSanitizer comes with AddressSanitizer on Linux). With MSVC or clang-cl, `address` builds with
`/fsanitize=address` on the DLL C runtime (`/MD`); clang-cl links the 32-bit runtime CMake finds on `LIB`
(`clang_rt.asan_dynamic-i386.lib` and its thunk: Visual Studio's x86 environment has them, LLVM's installer has only
64-bit ones) and copies `clang_rt.asan_dynamic-i386.dll` beside each exe. Windows ASan has no leak checker, so the
scripts set `detect_leaks=1` on Linux only. A sanitized replay run (`tools/replay_check.py --exe build-asan/ctr_native`) fails on any
sanitizer report.

## Memory Ledger

Internal builds sample the process's own memory each time a level finishes loading. The game heap (MEMPACK) is a
fixed arena that every level resets, so memory that grows when the same level is entered again is a host-side leak.
The log then says `[CTR Ledger] possible leak: ...`, which fails `tools/replay_check.py`, and every run ends with:

```text
[CTR Ledger] 14 level entries: process memory 210.3 MiB at the first, 236.0 MiB peak, 231.8 MiB at the last, 0 leak warning(s)
```

## Pull Requests

`.github/workflows/checks.yml` runs `tools/check.py --sanitize` on Linux for every pull request (the match lock shows
SKIP there: the runner does not build the PSX toolchain).
