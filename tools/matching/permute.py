#!/usr/bin/env python3
"""A small random permuter for one remaining function, judged by GCC 2.8.1 against the retail exe.

It works in a sandbox (build/permute/<name>): a copy of the sources, tools, metadata, retail inputs and the matching
toolchain, so several can run beside each other and beside work in the checkout. In the function's region it tries
changes that keep behaviour:

- swapping two adjacent simple statements that neither read nor write what the other writes (stores through the
  same pointer only when both use different constant indexes);
- swapping two adjacent declarations without an initializer;

builds with remaining.py, scores with align.py (mismatched retail instructions, lower is better; then the operand
line-up), and keeps a change that scores better (or the same, now and then, to drift). The best source so far is
build/permute/<name>/best.c, and every kept step is in build/permute/<name>/log.txt.

    python tools/matching/permute.py start bots1 game/BOTS.c BOTS_ThTick_Drive \\
        --start "void BOTS_ThTick_Drive(struct Thread *botThread)" --end "\\n}\\n" --minutes 360
    python tools/matching/permute.py status            # best score of every sandbox
    python tools/matching/permute.py diff bots1        # the best source's changes against the checkout

`start` runs in the foreground; start several in the background for parallel runs.
"""

from __future__ import annotations

import argparse
import difflib
import random
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SANDBOXES = ROOT / "build/permute"
COPIED = [
    "game",
    "include",
    "tools",
    "metadata",
    "assets",
    "externals/maspsx",
    "externals/old-gcc/prebuilt/windows-i386",
    "externals/psx-binutils/prebuilt/windows-i386",
]

STATEMENT = re.compile(
    r"^(\t+)(?!(?:case |default:|goto |break|return|continue|if\b|else\b|for\b|while\b|do\b|switch\b))"
    r"[^\n#/{}]*;\s*$"
)
DECLARATION = re.compile(
    r"^\t(?:register\s+)?(?:struct\s+\w+|u8|u16|u32|s8|s16|s32|int|char|b32|unsigned int|short)\s+\**\w+"
    r"(?:\[\d+\])?(?:\s+CTR_PSX_REGISTER\(\"\$\d+\"\))?;(?:\s*//.*)?$"
)
IDENTIFIER = re.compile(r"[A-Za-z_]\w*")
CALL = re.compile(r"([A-Za-z_]\w*)\s*\(")
PURE_CALLS = re.compile(r"^(?:CTR_Mips\w+|sizeof)$")
ASSIGNMENT = re.compile(r"^\s*(.+?)\s*(?:[-+|&^*/%]|<<|>>)?=(?!=)\s*(.*?);\s*(?://.*)?$")
MEMORY = re.compile(r"([A-Za-z_]\w*)\s*(?:\[|->|\.)")
KEYWORDS = {"if", "else", "while", "for", "return", "sizeof", "struct", "register", "u8", "u16", "u32", "s8", "s16",
            "s32", "int", "char", "unsigned", "short", "void", "const"}


# An lvalue chain: an optional dereference, a base name, then members and subscripts.
CHAIN = re.compile(r"(\*\s*)?([A-Za-z_]\w*)((?:\s*(?:->|\.)\s*[A-Za-z_]\w*|\s*\[[^\]]*\])*)")
COMPOUND = re.compile(r"(?:[-+|&^*/%]|<<|>>)=")


class Access:
    """One place a statement reads or writes: base name, member/subscript path, and whether it can be pinned down."""

    def __init__(self, deref: bool, base: str, path: str):
        self.base = base
        self.parts = re.findall(r"->\s*\w+|\.\s*\w+|\[[^\]]*\]", path)
        self.parts = [re.sub(r"\s+", "", part) for part in self.parts]
        self.deref = deref
        # set on the base value a store through a pointer reads implicitly
        self.implicit = False
        # a plain name with no path reads or writes the variable itself (or, for a struct, all of it)
        self.bare = not deref and not self.parts
        self.fixed = all(not part.startswith("[") or re.fullmatch(r"\[\d+\]", part) for part in self.parts)

    def overlaps(self, other: "Access") -> bool:
        if self.base != other.base:
            return False  # different names are taken not to alias, as the game's pointers do not here
        if self.bare or other.bare or self.deref or other.deref or not self.fixed or not other.fixed:
            return True
        shorter = min(len(self.parts), len(other.parts))
        return self.parts[:shorter] == other.parts[:shorter]


def accesses(expression: str) -> list[Access]:
    found = []
    for deref, base, path in CHAIN.findall(expression):
        if base in KEYWORDS or base.isdigit():
            continue
        found.append(Access(bool(deref), base, path))
    return found


def statement_effects(line: str) -> tuple[Access, list[Access]] | None:
    """(what the statement writes, everything it reads), or None when it is not a plain assignment safe to move."""
    if "++" in line or "--" in line or "goto" in line:
        return None
    for name in CALL.findall(line):
        if name not in KEYWORDS and not PURE_CALLS.match(name):
            return None
    match = ASSIGNMENT.match(line)
    if not match:
        return None
    left, right = match.groups()
    targets = accesses(left)
    if not targets:
        return None
    target = targets[0]
    reads = accesses(right)
    # the target's own subscripts and, for a store through it, the base pointer are read too
    for part in target.parts:
        if part.startswith("["):
            reads += accesses(part[1:-1])
    if not target.bare:
        # the base's own value: only a write to that variable itself changes it
        base_value = Access(False, target.base, "")
        base_value.implicit = True
        reads.append(base_value)
    if COMPOUND.search(line.split(";")[0]):
        reads.append(target)
    return target, reads


def conflicts(write: Access, reads: list[Access]) -> bool:
    for read in reads:
        if read.implicit:
            if write.bare and write.base == read.base:
                return True
            continue
        if write.overlaps(read):
            return True
        # a store through a pointer conflicts with any read of that pointer's own value: it may be an alias
        if read.bare and read.base == write.base:
            return True
    return False


def independent(first: str, second: str) -> bool:
    a = statement_effects(first)
    b = statement_effects(second)
    if a is None or b is None:
        return False
    write_a, reads_a = a
    write_b, reads_b = b
    if write_a.overlaps(write_b):
        return False
    return not conflicts(write_a, reads_b) and not conflicts(write_b, reads_a)


def mutations(lines: list[str]) -> list[int]:
    """Indexes i where lines i and i+1 may be swapped."""
    found = []
    for i in range(len(lines) - 1):
        first, second = lines[i], lines[i + 1]
        if DECLARATION.match(first) and DECLARATION.match(second):
            found.append(i)
            continue
        statement_a, statement_b = STATEMENT.match(first), STATEMENT.match(second)
        if statement_a and statement_b and statement_a.group(1) == statement_b.group(1) and independent(first, second):
            found.append(i)
    return found


def region(text: str, start: str, end: str) -> tuple[int, int]:
    begin = text.index(start)
    finish = text.index(end, begin + len(start))
    return begin, finish


def score(sandbox: Path, function: str) -> tuple[int, float] | None:
    build = subprocess.run([sys.executable, "tools/matching/remaining.py", function], cwd=sandbox,
                           capture_output=True, text=True)
    if "ERROR" in build.stdout or build.returncode not in (0, 1):
        return None
    if re.search(rf"^{re.escape(function)}\s+\S+\s+MATCH", build.stdout, re.M):
        return (0, 100.0)
    aligned = subprocess.run([sys.executable, "tools/matching/align.py", function], cwd=sandbox,
                             capture_output=True, text=True)
    found = re.search(r"with operands ([\d.]+)%, mismatched (\d+)", aligned.stdout)
    if not found:
        return None
    return (int(found.group(2)), float(found.group(1)))


def better(a: tuple[int, float], b: tuple[int, float]) -> bool:
    return a[0] < b[0] or (a[0] == b[0] and a[1] > b[1])


def make_sandbox(name: str) -> Path:
    sandbox = SANDBOXES / name
    if sandbox.exists():
        shutil.rmtree(sandbox)
    for relative in COPIED:
        source = ROOT / relative
        if not source.exists():
            raise SystemExit(f"{source} is missing (the Windows toolchain is built by tools/matching/windows/build-toolchain.sh)")
        shutil.copytree(source, sandbox / relative)
    return sandbox


def start(args: argparse.Namespace) -> int:
    start_marker = args.start.encode().decode("unicode_escape")
    end_marker = args.end.encode().decode("unicode_escape")
    sandbox = make_sandbox(args.name)
    path = sandbox / args.source
    (sandbox / "base.c").write_bytes(path.read_bytes())
    log = open(sandbox / "log.txt", "a", buffering=1)
    rng = random.Random(args.seed)
    deadline = time.time() + args.minutes * 60

    with open(path, newline="") as stream:
        text = stream.read()
    best_score = score(sandbox, args.function)
    if best_score is None:
        print("the function does not build or score", file=sys.stderr)
        return 1
    best = text
    current, current_score = text, best_score
    log.write(f"start {args.function} {best_score}\n")
    (sandbox / "best.c").write_text(best, newline="")
    tries = 0
    while time.time() < deadline and best_score[0] > 0:
        tries += 1
        begin, finish = region(current, start_marker, end_marker)
        body = current[begin:finish]
        newline = "\r\n" if "\r\n" in body else "\n"
        lines = body.split(newline)
        what = []
        for _ in range(rng.choice((1, 1, 2, 3))):
            options = mutations(lines)
            if not options:
                break
            i = rng.choice(options)
            lines[i], lines[i + 1] = lines[i + 1], lines[i]
            what.append(str(i))
        if not what:
            break
        candidate = current[:begin] + newline.join(lines) + current[finish:]
        path.write_text(candidate, newline="")
        result = score(sandbox, args.function)
        if result is None:
            path.write_text(current, newline="")
            continue
        if better(result, current_score) or (result == current_score and rng.random() < 0.3):
            current, current_score = candidate, result
            if better(result, best_score):
                best, best_score = candidate, result
                (sandbox / "best.c").write_text(best, newline="")
                log.write(f"{time.strftime('%H:%M:%S')} try {tries} swap {'+'.join(what)} -> {result}\n")
        else:
            path.write_text(current, newline="")
    log.write(f"end tries {tries} best {best_score}\n")
    return 0


def status(_: argparse.Namespace) -> int:
    for sandbox in sorted(SANDBOXES.glob("*")):
        log = sandbox / "log.txt"
        if log.is_file():
            lines = log.read_text().splitlines()
            print(f"{sandbox.name:10} {lines[-1] if lines else ''}")
    return 0


def diff(args: argparse.Namespace) -> int:
    sandbox = SANDBOXES / args.name
    base = (sandbox / "base.c").read_text().splitlines(keepends=True)
    best = (sandbox / "best.c").read_text().splitlines(keepends=True)
    sys.stdout.writelines(difflib.unified_diff(base, best, "base.c", "best.c"))
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Random permuter for a remaining function.")
    commands = parser.add_subparsers(dest="command", required=True)
    run = commands.add_parser("start")
    run.add_argument("name")
    run.add_argument("source")
    run.add_argument("function")
    run.add_argument("--start", required=True, help="text that opens the region to permute")
    run.add_argument("--end", required=True, help="text that closes it")
    run.add_argument("--minutes", type=float, default=60)
    run.add_argument("--seed", type=int)
    run.set_defaults(func=start)
    commands.add_parser("status").set_defaults(func=status)
    show = commands.add_parser("diff")
    show.add_argument("name")
    show.set_defaults(func=diff)
    args = parser.parse_args()
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
