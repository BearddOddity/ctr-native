# Regression Safeguards

One command runs every check this machine can run:

```sh
python3 tools/check.py                         # matching tests, match lock, native build and tests
python3 tools/check.py --sanitize              # plus an AddressSanitizer/UBSan build and its tests
python3 tools/check.py --replays debug/reports # plus every replay you recorded (needs your disc's assets)
```

A step this machine can't run says SKIP with the reason; a skip is never counted as a pass.

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
`/fsanitize=address`. A sanitized replay run (`tools/replay_check.py --exe build-asan/ctr_native`) fails on any
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
