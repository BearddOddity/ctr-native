#!/usr/bin/env python3
"""Score the game functions no matching target covers yet (metadata/retail/ntsc-u-926/remaining.json).

Each one is built the way a resident target builds its functions: its whole source file compiled with GCC 2.8.1 and
maspsx, with the flags and forced includes of the unit it sits beside (`like`), then linked at its retail address with
the retail symbol table and compared word by word with the retail exe.

    python3 tools/matching/remaining.py                  # score all of them (needs the retail exe in assets/)
    python3 tools/matching/remaining.py CAM_ThTick       # score one
    python3 tools/matching/remaining.py --compile-only   # no exe: does each build, and how big is ours

The exe never leaves the PC it is on: without it, only --compile-only runs. A function that reaches MATCH here goes
into a real target in matching.json (then `lock.py update`) and comes out of remaining.json.
"""

from __future__ import annotations

import argparse
import copy
import json
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import pipeline  # noqa: E402
import resident  # noqa: E402

REMAINING = pipeline.ROOT / "metadata/retail/ntsc-u-926/remaining.json"


def load_remaining(path: Path = REMAINING) -> list[dict]:
    data = json.loads(path.read_text())
    entries = data.get("functions")
    if not isinstance(entries, list) or not entries:
        raise pipeline.MatchError(f"{path}: needs a non-empty functions list")
    names = [entry.get("name") for entry in entries]
    if len(set(names)) != len(names) or not all(isinstance(name, str) for name in names):
        raise pipeline.MatchError(f"{path}: every function needs a name, once")
    return entries


def target_for(manifest: dict, entry: dict) -> dict:
    """A resident target covering exactly this function, with the flags of the unit beside it."""
    like = pipeline.target_by_name(manifest, entry["like"])
    target = {
        key: copy.deepcopy(like[key])
        for key in ("kind", "artifact", "symbol_file", "include_directories", "compiler_flags",
                    "small_data_limit", "aspsx_version", "forced_includes", "link_symbols")
        if key in like
    }
    target.update(
        name=f"remaining-{entry['name']}".replace("_", "-"),
        start_symbol=entry["name"],
        end_symbol=entry["end_symbol"],
        symbol_prefix=entry["name"],
        source=entry["source"],
    )
    for key in ("compiler_flags", "small_data_limit", "forced_includes", "aspsx_version", "link_symbols"):
        if key in entry:
            target[key] = copy.deepcopy(entry[key])
    # The unit beside it may bind this very function to its retail address (to call it); here it is the one built.
    if entry["name"] in target.get("link_symbols", {}):
        target["link_symbols"] = {k: v for k, v in target["link_symbols"].items() if k != entry["name"]}
    return target


def retail_size(target: dict) -> int:
    symbols = resident.parse_symbols(pipeline.repository_path(target["symbol_file"]))
    by_name = {symbol.name: symbol.address for symbol in symbols}
    for key in ("start_symbol", "end_symbol"):
        if target[key] not in by_name:
            raise pipeline.MatchError(f"{target[key]} is not in {target['symbol_file']}")
    size = by_name[target["end_symbol"]] - by_name[target["start_symbol"]]
    if size <= 0:
        raise pipeline.MatchError(f"{target['end_symbol']} does not come after {target['start_symbol']}")
    return size


def compiled_size(toolchain: pipeline.Toolchain, target: dict, name: str, out: Path) -> int | str:
    """Our function's size in the object, or why the file doesn't build."""
    try:
        obj, _ = pipeline.build_object(toolchain, target, pipeline.ROOT / target["source"], out / Path(target["source"]).stem)
    except subprocess.CalledProcessError as exc:
        return f"does not build (exit {exc.returncode})"
    except pipeline.MatchError as exc:
        return f"does not build: {str(exc).splitlines()[-1] if str(exc) else exc}"
    headers = subprocess.run([str(toolchain.binutils["objdump"]), "-h", str(obj)], text=True,
                             capture_output=True, check=True).stdout
    for line in headers.splitlines():
        parts = line.split()
        if len(parts) >= 3 and parts[0].isdigit() and parts[1].removeprefix(".text.").lstrip(".") == name:
            return int(parts[2], 16)
    return "not in the object"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("names", nargs="*", help="functions to score (default: all in remaining.json)")
    parser.add_argument("--compile-only", action="store_true", help="no retail exe: build and size only")
    parser.add_argument("--reference-root", help="folder holding the retail files (default: assets/)")
    parser.add_argument("--json", help="also write the results to this file")
    args = parser.parse_args()

    try:
        manifest = pipeline.load_json(pipeline.DEFAULT_MANIFEST)
        entries = load_remaining()
        if unknown := sorted(set(args.names) - {entry["name"] for entry in entries}):
            raise pipeline.MatchError(f"not in remaining.json: {', '.join(unknown)}")
        chosen = [entry for entry in entries if not args.names or entry["name"] in args.names]
        toolchain = pipeline.resolve_toolchain(manifest)
        references = pipeline.reference_root(manifest, args.reference_root)
        if not args.compile_only:
            checked = pipeline.verify_references(manifest, references, {"exe"})
            bad = [result for result in checked if result["errors"]]
            if bad:
                print("The retail exe is not here or not the disc's own:", "; ".join(bad[0]["errors"]))
                print("Run with --compile-only to build and size them without it.")
                return 2
    except pipeline.MatchError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 2

    results = []
    out = pipeline.BUILD / "remaining"
    for entry in chosen:
        target = target_for(manifest, entry)
        expected = retail_size(target)
        row = {"name": entry["name"], "source": entry["source"], "retail_bytes": expected}
        if args.compile_only:
            ours = compiled_size(toolchain, target, entry["name"], out / entry["name"])
            row["our_bytes" if isinstance(ours, int) else "error"] = ours
            shown = f"ours {ours} bytes" if isinstance(ours, int) else ours
            print(f"{entry['name']:<26} retail {expected:>6} bytes, {shown}")
        else:
            manifest_here = dict(manifest, targets=list(manifest["targets"]) + [target])
            try:
                result = resident.build_resident(manifest_here, toolchain, target, references)
            except (pipeline.MatchError, subprocess.CalledProcessError) as exc:
                result = {"functions": [{"name": entry["name"], "error": str(exc).splitlines()[-1] if str(exc) else repr(exc)}]}
            function = next((f for f in result.get("functions", []) if f.get("name") == entry["name"]), None)
            if function is None or function.get("error"):
                row["error"] = (function or {}).get("error", "no result")
            else:
                row.update(exact=function["exact"], matching_words=function["matching_words"],
                           total_words=function["total_words"], matching_bytes=function["matching_bytes"])
        results.append(row)

    if not args.compile_only:
        print()
        print(f"{'function':<26} {'words alike':>14}  result")
        for row in results:
            if "error" in row:
                print(f"{row['name']:<26} {'':>14}  ERROR {row['error']}")
            else:
                share = 100.0 * row["matching_words"] / row["total_words"] if row["total_words"] else 0.0
                state = "MATCH" if row["exact"] else f"{share:.1f}%"
                print(f"{row['name']:<26} {row['matching_words']:>6}/{row['total_words']:<7}  {state}")
    if args.json:
        Path(args.json).write_text(json.dumps(results, indent=2) + "\n")
    return 0 if args.compile_only or all(row.get("exact") for row in results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
