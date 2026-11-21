#!/usr/bin/env python3
"""Replay regression check: play every recorded replay back and fail on any divergence.

A replay (`ctr_native --record`, docs/REPLAYS.md) holds the pad input and the game's per-frame state (frame counters,
level, RNG seeds). Playback feeds the same input and compares the state each frame, so a change that alters gameplay
shows as `divergence at replay frame N`. Replays live in your own debug/reports folder (they come from your copy of the
game) and are never committed.

    python3 tools/replay_check.py                       # every replay under debug/reports, unthrottled
    python3 tools/replay_check.py --replays D:/ctr/replays --exe build/ctr_native
    python3 tools/replay_check.py --realtime            # at PS1 speed (to watch, or to measure pacing)

Each run uses --replay-bypass-header (a replay records the build that made it; a newer build is what this checks) and
--replay-fast (VBlanks as fast as the host runs; game time stays the replay's). A sanitizer build's reports
(AddressSanitizer, LeakSanitizer, UBSan) fail the run too. Exit 0 = every replay finished with no divergence.
"""
from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BAD = re.compile(
    r"divergence at replay frame|checkpoint count mismatch|corrupt replay frame|failed to (read|install|restore|open)"
    r"|invalid replay|AddressSanitizer|LeakSanitizer|runtime error:|ERROR: |\[CTR Ledger\] possible leak"
)
FINISHED = re.compile(r"replay finished after (\d+) frames")
SPEED = re.compile(r"([\d.]+) s wall, ([\d.]+) game frames/s")


def default_exe() -> Path:
    for name in ("build/ctr_native.exe", "build/ctr_native", "build-msvc-x86/Release/ctr_native.exe"):
        if (ROOT / name).exists():
            return ROOT / name
    return ROOT / "build" / ("ctr_native.exe" if os.name == "nt" else "ctr_native")


def run_one(exe: Path, replay: Path, fast: bool, timeout: int, extra: list[str]) -> dict:
    command = [str(exe), "--replay", str(replay), "--replay-bypass-header", *(["--replay-fast"] if fast else []), *extra]
    started = time.monotonic()
    env = dict(os.environ)
    env.setdefault("ASAN_OPTIONS", "detect_leaks=1:abort_on_error=0")
    env.setdefault("UBSAN_OPTIONS", "print_stacktrace=1")
    try:
        done = subprocess.run(command, cwd=exe.parent, capture_output=True, text=True, errors="replace",
                              timeout=timeout, env=env)
        output, code = done.stdout + done.stderr, done.returncode
    except subprocess.TimeoutExpired as exc:
        output = (exc.stdout or "") + (exc.stderr or "")
        output = output if isinstance(output, str) else output.decode(errors="replace")
        return {"replay": str(replay), "ok": False, "why": f"no end within {timeout} s", "seconds": timeout}
    bad = next((line.strip() for line in output.splitlines() if BAD.search(line)), None)
    finished = FINISHED.search(output)
    speed = SPEED.search(output)
    result = {
        "replay": str(replay),
        "exit": code,
        "frames": int(finished.group(1)) if finished else None,
        "seconds": round(time.monotonic() - started, 2),
        "frames_per_second": float(speed.group(2)) if speed else None,
    }
    if bad:
        result.update(ok=False, why=bad)
    elif not finished:
        result.update(ok=False, why=f"exit {code} before the replay finished")
    elif code != 0:
        result.update(ok=False, why=f"exit {code}")
    else:
        result.update(ok=True, why="")
    return result


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--exe", type=Path, default=None, help="the ctr_native build to check (default: build/)")
    parser.add_argument("--replays", type=Path, default=None, help="folder searched for *.ctrreplay (default: debug/reports beside the exe)")
    parser.add_argument("--realtime", action="store_true", help="play at PS1 speed instead of unthrottled")
    parser.add_argument("--timeout", type=int, default=1800, help="seconds per replay")
    parser.add_argument("--json", type=Path, help="write the results here")
    parser.add_argument("extra", nargs="*", help="more ctr_native arguments (after --)")
    args = parser.parse_args(argv)

    exe = (args.exe or default_exe()).resolve()
    if not exe.exists():
        print(f"replay check: no ctr_native at {exe} (build it first, or pass --exe)", file=sys.stderr)
        return 2
    folder = (args.replays or exe.parent / "debug" / "reports").resolve()
    replays = sorted(folder.rglob("*.ctrreplay")) if folder.is_dir() else []
    if not replays:
        print(f"replay check: no *.ctrreplay under {folder} (record one: ctr_native --record)", file=sys.stderr)
        return 2

    results = []
    for replay in replays:
        result = run_one(exe, replay, not args.realtime, args.timeout, args.extra)
        results.append(result)
        fps = f", {result['frames_per_second']:.0f} frames/s" if result.get("frames_per_second") else ""
        frames = f"{result['frames']} frames" if result.get("frames") is not None else "no end"
        status = "PASS" if result["ok"] else "FAIL"
        print(f"{status}  {replay.relative_to(folder)}: {frames}, {result['seconds']} s{fps}"
              + (f"  ({result['why']})" if result["why"] else ""))
    if args.json:
        args.json.write_text(json.dumps(results, indent=1) + "\n")
    failed = sum(not r["ok"] for r in results)
    print(f"replay check: {len(results) - failed}/{len(results)} replays played back with no divergence")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
