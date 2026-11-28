#!/usr/bin/env python3
"""How close a remaining function is to retail once the instructions are lined up, not just compared in place.

remaining.py counts words that are equal at the same offset, so one extra instruction early on makes everything
after it count as different. This lines the two instruction lists up (difflib) and reports:

- opcodes alike: the share of instructions whose mnemonic lines up;
- with operands: the same with operands too (branch and call targets are ignored, as they move with the code);
- mismatched: how many retail instructions are not part of a lined-up run. Lower is better; it is steadier than the
  two ratios, which can jump when the line-up shifts.

    python tools/matching/remaining.py BOTS_ThTick_Drive     # build first
    python tools/matching/align.py BOTS_ThTick_Drive          # scores
    python tools/matching/align.py BOTS_ThTick_Drive --blocks 10   # and the ten biggest differing runs
"""

from __future__ import annotations

import argparse
import difflib
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
NAMESPACES = ROOT / "build/matching/ntsc-u-926/namespaces"
TARGET = re.compile(r"0x8[0-9a-f]{7}")


def instructions(path: Path) -> list[tuple[str, str, str]]:
    found = []
    for line in path.read_text().splitlines():
        parts = line.split("\t")
        if len(parts) >= 3:
            operands = parts[3].strip() if len(parts) > 3 else ""
            found.append((parts[0].strip(" :"), parts[2].strip(), TARGET.sub("ADDR", operands)))
    return found


def main() -> int:
    parser = argparse.ArgumentParser(description="Line up a remaining function's instructions with retail's.")
    parser.add_argument("function")
    parser.add_argument("--blocks", type=int, default=0, help="also list this many of the biggest differing runs")
    args = parser.parse_args()

    folders = sorted(NAMESPACES.glob(f"remaining-*/functions/{args.function}"))
    if not folders:
        print(f"no build of {args.function}: run tools/matching/remaining.py {args.function} first", file=sys.stderr)
        return 1
    folder = folders[0]
    retail = instructions(folder / "retail.objdump")
    ours = instructions(folder / "candidate.objdump")

    opcodes = difflib.SequenceMatcher(None, [i[1] for i in retail], [i[1] for i in ours], autojunk=False)
    full = difflib.SequenceMatcher(
        None, [f"{i[1]} {i[2]}" for i in retail], [f"{i[1]} {i[2]}" for i in ours], autojunk=False
    )
    mismatched = sum(
        max(i2 - i1, 1) for tag, i1, i2, _, _ in full.get_opcodes() if tag != "equal"
    )
    print(
        f"{args.function}: retail {len(retail)} instructions, ours {len(ours)}; opcodes alike "
        f"{opcodes.ratio() * 100:.1f}%, with operands {full.ratio() * 100:.1f}%, mismatched {mismatched}"
    )

    if args.blocks:
        runs = [
            (max(i2 - i1, j2 - j1), tag, i1, i2, j1, j2)
            for tag, i1, i2, j1, j2 in opcodes.get_opcodes()
            if tag != "equal"
        ]
        for size, tag, i1, i2, j1, j2 in sorted(runs, reverse=True)[: args.blocks]:
            where = retail[i1][0] if i1 < len(retail) else "end"
            print(f"\n{size:3} {tag} at retail {where} [{i1}:{i2}], ours [{j1}:{j2}]")
            for _, op, operands in retail[i1 : min(i2, i1 + 8)]:
                print(f"    - {op} {operands}")
            for _, op, operands in ours[j1 : min(j2, j1 + 8)]:
                print(f"    + {op} {operands}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
