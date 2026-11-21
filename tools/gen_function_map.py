#!/usr/bin/env python3
"""The game's function map: every function the game defines, by a hash of its name.

A checkpoint (quick states, a replay's bootstrap state) holds the game's memory as it was, and with it every function
pointer the game stored there: a thread's tick, a driver's state functions, menu callbacks, the HOWL opcode table.
Another build (a newer commit, the sanitizer build) puts its functions at other addresses, so a checkpoint carries
this map's (name hash, address) pairs of the build that wrote it, and a restore turns each stored function address
into the live build's address of the same function by name.

The map is written from the game's own sources as the compiler sees them (`cc -E` of main.c with the native defines),
so a function another `#if` leaves out is not in it:

    python3 tools/gen_function_map.py            # write platform/native_function_map.inc
    python3 tools/gen_function_map.py --check    # exit 1 when it is out of date (tools/check.py runs this)

Exit 2 = no C preprocessor here (gcc, clang or cc), skipped.
"""
from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "platform" / "native_function_map.inc"
DEFINES = ["CTR_NATIVE", "CTR_INTERNAL", 'CTR_NATIVE_VERSION="map"', 'CTR_NATIVE_BUILD_ID="map"']
TOKEN = re.compile(r'''
    (?P<marker>^\#\s*\d+\s+"(?P<file>[^"]*)".*$)
  | (?P<string>"(?:\\.|[^"\\\n])*"|'(?:\\.|[^'\\\n])*')
  | (?P<ident>[A-Za-z_]\w*)
  | (?P<number>\.?\d[\w.]*(?:[eEpP][+-]\w*)?)
  | (?P<punct>[{}()\[\];,=])
  | (?P<other>\S)
''', re.VERBOSE | re.MULTILINE)
SKIP_ATTRS = {"__attribute__", "__declspec", "__asm__", "__asm", "asm"}


def fnv1a(name: str) -> int:
    value = 2166136261
    for byte in name.encode("ascii"):
        value = ((value ^ byte) * 16777619) & 0xFFFFFFFF
    return value


def preprocessor() -> list[str] | None:
    for name in (os.environ.get("CC"), "gcc", "clang", "cc"):
        if name and shutil.which(name):
            return [shutil.which(name)]
    return None


def preprocess(cc: list[str]) -> str:
    command = [*cc, "-E", "-std=c17", *(f"-D{d}" for d in DEFINES), f"-I{ROOT / 'include'}",
               f"-I{ROOT / 'externals' / 'SDL' / 'include'}", str(ROOT / "main.c")]
    with tempfile.TemporaryDirectory() as scratch:
        out = Path(scratch) / "unity.i"
        done = subprocess.run([*command, "-o", str(out)], cwd=ROOT, capture_output=True, text=True, errors="replace")
        if done.returncode != 0:
            raise SystemExit(f"function map: the preprocessor failed:\n{done.stderr[-2000:]}")
        return out.read_text(errors="replace")


def game_file(path: str) -> bool:
    try:
        rel = Path(path).resolve().relative_to(ROOT).as_posix()
    except ValueError:
        rel = path.replace("\\", "/")
    return rel.startswith("game/")


def definitions(text: str) -> list[str]:
    """Names of the functions defined at file scope in game/ sources, in order."""
    tokens: list[tuple[str, str, str]] = []  # (kind, text, file)
    current = ""
    for m in TOKEN.finditer(text):
        if m.group("marker"):
            current = m.group("file")
            continue
        kind = m.lastgroup
        if kind == "string":
            tokens.append(("string", "", current))
        else:
            tokens.append((kind, m.group(kind), current))

    names: list[str] = []
    depth = 0
    statement_start = 0  # first token of the current file-scope declaration
    for index, (kind, value, _file) in enumerate(tokens):
        if value == "{":
            if depth == 0:
                name = function_name(tokens, index, statement_start)
                if name is not None:
                    names.append(name)
            depth += 1
        elif value == "}":
            depth -= 1
            if depth == 0:
                statement_start = index + 1
        elif value == ";" and depth == 0:
            statement_start = index + 1
    return names


def matching_open(tokens: list, close: int) -> int:
    level = 0
    for i in range(close, -1, -1):
        if tokens[i][1] == ")":
            level += 1
        elif tokens[i][1] == "(":
            level -= 1
            if level == 0:
                return i
    return -1


def function_name(tokens: list, brace: int, start: int) -> str | None:
    i = brace - 1
    while i >= start and tokens[i][1] == ")":
        open_index = matching_open(tokens, i)
        if open_index <= start:
            return None
        before = open_index - 1
        # `__attribute__((...))` after the parameters: step over it to the parameters' `)`.
        if tokens[before][1] == "(":
            before -= 1
        if tokens[before][1] in SKIP_ATTRS:
            i = before - 1
            continue
        if tokens[before][0] != "ident" or not game_file(tokens[before][2]):
            return None
        words = {t[1] for t in tokens[start:before] if t[0] == "ident"}
        if "typedef" in words or "struct" in words and tokens[before - 1][1] == "struct":
            return None
        # C99 `inline` with neither static nor extern makes no definition to take the address of.
        if "inline" in words and not ({"static", "extern"} & words):
            return None
        return tokens[before][1]
    return None


def render(names: list[str]) -> str:
    seen: dict[int, str] = {}
    for name in names:
        h = fnv1a(name)
        if h in seen and seen[h] != name:
            raise SystemExit(f"function map: {name} and {seen[h]} share the name hash 0x{h:08x}")
        seen[h] = name
    lines = ["// Written by tools/gen_function_map.py from the game's sources: run it again after adding or removing a",
             "// function (tools/check.py says when). One line per game function: FNV-1a hash of its name, its name.",
             f"// {len(seen)} functions, sorted by hash."]
    lines += [f"NATIVE_FUNCTION_ID(0x{h:08x}u, {seen[h]})" for h in sorted(seen)]
    return "\n".join(lines) + "\n"


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--check", action="store_true", help="exit 1 when platform/native_function_map.inc is out of date")
    args = parser.parse_args(argv)
    cc = preprocessor()
    if cc is None:
        print("function map: no C preprocessor (gcc, clang or cc) on PATH; skipped", file=sys.stderr)
        return 2
    text = render(definitions(preprocess(cc)))
    if args.check:
        if not OUT.exists() or OUT.read_text() != text:
            print(f"function map: {OUT.relative_to(ROOT)} is out of date; run python3 tools/gen_function_map.py")
            return 1
        print(f"function map: {OUT.relative_to(ROOT)} is up to date ({text.count('NATIVE_FUNCTION_ID(')} functions)")
        return 0
    OUT.write_text(text)
    print(f"function map: {text.count('NATIVE_FUNCTION_ID(')} functions written to {OUT.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
