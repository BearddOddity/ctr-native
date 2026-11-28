#!/usr/bin/env python3
"""Report how much of the retail exe's code is proven byte-exact, and name the gaps.

Reads every resident target's result.json under build/matching/ntsc-u-926/namespaces (so run the targets first,
for example with `match.py build <target>` for each one), takes the union of the address ranges of every exact
function and every exact linked-block interval, and walks the exe's code span looking for bytes no exact range
covers. Each gap is printed with the retail symbols that fall inside it, which is how a function that no target
and no remaining.json entry covers is found.

    python3 tools/matching/coverage.py
    python3 tools/matching/coverage.py --json gaps.json
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
RESULTS = ROOT / "build/matching/ntsc-u-926/namespaces"
PROBES = ROOT / "build/matching/ntsc-u-926/probes"
SYMBOLS = ROOT / "metadata/retail/ntsc-u-926/symbols/syms926.txt"
REMAINING = ROOT / "metadata/retail/ntsc-u-926/remaining.json"

# The exe's code runs from the first function after .rodata to the start of .data.
TEXT_START = 0x800123E0
TEXT_END = 0x800809A0


def load_symbols() -> list[tuple[int, str]]:
    symbols = []
    for line in SYMBOLS.read_text().splitlines():
        parts = line.split()
        if len(parts) >= 2 and len(parts[0]) == 8:
            try:
                symbols.append((int(parts[0], 16), parts[1]))
            except ValueError:
                pass
    return sorted(symbols)


def exact_intervals() -> tuple[list[tuple[int, int]], list[str]]:
    intervals: list[tuple[int, int]] = []
    inexact: list[str] = []
    for path in sorted(RESULTS.glob("*/result.json")):
        result = json.loads(path.read_text())
        for function in result.get("functions") or []:
            start = int(function["address"], 16)
            size = function.get("expected_size") or function["size"]
            if function.get("exact"):
                intervals.append((start, start + size))
            else:
                inexact.append(function["name"])
        block = result.get("linked_block") or {}
        for key in ("code_intervals", "rodata_intervals", "data_intervals", "bss_intervals"):
            for interval in block.get(key) or []:
                if "address" in interval and interval.get("matching_bytes") == interval.get("expected_size"):
                    start = int(interval["address"], 16)
                    intervals.append((start, start + interval["expected_size"]))
    # A probe compares one extracted function on its own; it proves its range when its region is the exe.
    for path in sorted(PROBES.glob("*/*/result.json")):
        result = json.loads(path.read_text())
        if result.get("region") != "exe" or "address" not in result:
            continue
        start = int(result["address"], 16)
        if result.get("exact"):
            intervals.append((start, start + result["expected_size"]))
        else:
            inexact.append(result.get("symbol") or result["name"])
    return sorted(intervals), sorted(set(inexact))


def merge(intervals: list[tuple[int, int]]) -> list[list[int]]:
    merged: list[list[int]] = []
    for start, end in intervals:
        if merged and start <= merged[-1][1]:
            merged[-1][1] = max(merged[-1][1], end)
        else:
            merged.append([start, end])
    return merged


def main() -> int:
    parser = argparse.ArgumentParser(description="Report proven-exact coverage of the retail exe's code and name the gaps.")
    parser.add_argument("--json", help="also write the gaps to this file")
    args = parser.parse_args()

    if not RESULTS.is_dir():
        print(f"no results under {RESULTS}: build the targets first", file=sys.stderr)
        return 1

    symbols = load_symbols()
    intervals, inexact = exact_intervals()
    merged = merge(intervals)
    remaining = {entry["name"] for entry in json.loads(REMAINING.read_text())["functions"]}

    covered = sum(
        min(end, TEXT_END) - max(start, TEXT_START) for start, end in merged if end > TEXT_START and start < TEXT_END
    )
    total = TEXT_END - TEXT_START
    print(f"exe code 0x{total:x} bytes, 0x{covered:x} proven exact ({100 * covered / total:.3f}%)")
    if inexact:
        print("built but not exact: " + ", ".join(inexact))

    gaps = []
    cursor = TEXT_START
    for start, end in merged:
        if end <= TEXT_START or start >= TEXT_END:
            continue
        if start > cursor:
            gaps.append((cursor, start))
        cursor = max(cursor, end)
    if cursor < TEXT_END:
        gaps.append((cursor, TEXT_END))

    report = []
    print(f"{len(gaps)} gaps")
    for start, end in gaps:
        names = [name for address, name in symbols if start <= address < end]
        listed = [name for name in names if name in remaining]
        tag = "remaining.json" if listed and len(listed) == len(names) else "untracked"
        print(f"  {start:08x}-{end:08x} 0x{end - start:<5x} {tag:14} {' '.join(names[:8])}")
        report.append({"start": f"0x{start:08x}", "end": f"0x{end:08x}", "size": end - start,
                       "symbols": names, "tracked": tag == "remaining.json"})

    if args.json:
        Path(args.json).write_text(json.dumps({"covered": covered, "total": total, "gaps": report}, indent=1) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
