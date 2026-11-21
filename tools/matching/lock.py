"""Match lock: a fingerprint of every matching unit's compiled bytes, checked without the retail disc.

`verify`, `check` and `build` prove a match against the retail executable, which only a person's own copy holds.
This lock records what GCC 2.8.1 makes from today's sources: for every target in matching.json, each source's
allocated sections (a function's own `.<name>` section, or a whole overlay's .text/.data/...) as their bytes and
relocations, hashed. `check` compiles them again and names every section whose bytes or relocations changed, so an
edit that breaks a matched function is caught on any machine with the vendored toolchain, no exe needed.

    python3 tools/matching/lock.py check            # exit 1 when a locked section changed, appeared or went away
    python3 tools/matching/lock.py update           # write the lock again (after a deliberate change, verified)
    python3 tools/matching/lock.py check --target load-queue-next

A resident target locks only the functions it owns (its inventory) plus the data sections its compare covers
(`linked_block`, data or BSS ranges); an artifact or data target locks every allocated section. A function moved
from `__asm__` to C that compiles to the same bytes and relocations keeps its lock entry: the lock passes.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import struct
import sys
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path
from typing import Any

sys.path.insert(0, str(Path(__file__).resolve().parent))
import pipeline as ctr_match  # noqa: E402
import resident as ctr_resident  # noqa: E402

LOCK_FILE = ctr_match.METADATA / "match_lock.json"
LOCK_BUILD = ctr_match.BUILD / "lock"
LOCK_SCHEMA = 1

SHF_ALLOC = 0x2
SHT_PROGBITS, SHT_SYMTAB, SHT_RELA, SHT_NOBITS, SHT_REL = 1, 2, 4, 8, 9
STT_SECTION = 3


def elf_sections(raw: bytes) -> dict[str, dict[str, Any]]:
    """Every allocated section of a relocatable little-endian MIPS ELF32: bytes (or size) and relocations."""
    if raw[:6] != b"\x7fELF\x01\x01" or struct.unpack_from("<HH", raw, 16) != (1, 8):
        raise ctr_match.MatchError("the lock reads relocatable little-endian MIPS ELF32 objects only")
    shoff, = struct.unpack_from("<I", raw, 32)
    shentsize, shnum, shstrndx = struct.unpack_from("<HHH", raw, 46)
    headers = [struct.unpack_from("<IIIIIIIIII", raw, shoff + i * shentsize) for i in range(shnum)]
    strtab = headers[shstrndx]

    def cstr(table: tuple, offset: int) -> str:
        start = table[4] + offset
        return raw[start:raw.index(b"\0", start)].decode("latin-1")

    names = [cstr(strtab, h[0]) for h in headers]
    symbols: list[str] = []
    for h in headers:
        if h[1] == SHT_SYMTAB:
            link = headers[h[6]]
            for off in range(h[4], h[4] + h[5], 16):
                name, _value, _size, info, _other, shndx = struct.unpack_from("<IIIBBH", raw, off)
                if (info & 0xF) == STT_SECTION and shndx < len(names):
                    symbols.append(names[shndx])
                else:
                    symbols.append(cstr(link, name))
    relocs: dict[int, list[tuple[int, int, str, int]]] = {}
    for h in headers:
        if h[1] in (SHT_REL, SHT_RELA):
            size = 12 if h[1] == SHT_RELA else 8
            rows = relocs.setdefault(h[7], [])
            for off in range(h[4], h[4] + h[5], size):
                r_offset, r_info = struct.unpack_from("<II", raw, off)
                addend = struct.unpack_from("<i", raw, off + 8)[0] if size == 12 else 0
                sym = r_info >> 8
                rows.append((r_offset, r_info & 0xFF, symbols[sym] if sym < len(symbols) else f"#{sym}", addend))
    out: dict[str, dict[str, Any]] = {}
    for index, h in enumerate(headers):
        if not (h[2] & SHF_ALLOC) or h[1] not in (SHT_PROGBITS, SHT_NOBITS) or h[5] == 0:
            continue
        data = b"" if h[1] == SHT_NOBITS else raw[h[4]:h[4] + h[5]]
        out[names[index]] = {"size": h[5], "data": data, "relocs": relocs.get(index, []), "nobits": h[1] == SHT_NOBITS}
    return out


R_MIPS_32, R_MIPS_26, R_MIPS_HI16, R_MIPS_LO16 = 2, 4, 5, 6


def readonly_name(name: str) -> str:
    """One name for read-only data: hand-written `__asm__` puts it in `.rdata`, GCC's own output in `.rodata` (GNU as
    turns the `.rdata` directive into `.rodata`), and the pipeline links either at the target's rodata address."""
    if name == ".rdata" or name.startswith(".rdata."):
        return ".rodata" + name[len(".rdata"):]
    return name


def address_of(name: str, symbols: dict[str, int]) -> int | None:
    """A relocation's retail address: its symbol's, or a function section's function's (`.NAME` -> NAME)."""
    name = readonly_name(name)
    if name in symbols:
        return symbols[name]
    if name.startswith(".") and name[1:] in symbols:
        return symbols[name[1:]]
    return None


def linked(section: dict[str, Any], symbols: dict[str, int]) -> tuple[bytes, list[tuple[int, int, str, int]]]:
    """Apply every relocation whose target has a retail address, as the linker would, so a function written with
    absolute addresses (`__asm__`) and one written against named symbols give the same words. The rest stay rows."""
    data = bytearray(section["data"])
    rows = section["relocs"]
    left = []
    word = lambda off: struct.unpack_from("<I", data, off)[0]
    for i, (off, kind, name, addend) in enumerate(rows):
        target = address_of(name, symbols)
        if target is None or section["nobits"] or off + 4 > len(data):
            left.append((off, kind, readonly_name(name), addend))
            continue
        insn = word(off)
        if kind == R_MIPS_32:
            struct.pack_into("<I", data, off, (insn + target) & 0xFFFFFFFF)
        elif kind == R_MIPS_26:
            dest = (target + ((insn & 0x3FFFFFF) << 2)) & 0xFFFFFFFF
            struct.pack_into("<I", data, off, (insn & 0xFC000000) | ((dest >> 2) & 0x3FFFFFF))
        elif kind == R_MIPS_HI16:
            lo = next((r for r in rows[i + 1:] if r[1] == R_MIPS_LO16 and r[2] == name), None)
            if lo is None:
                left.append((off, kind, readonly_name(name), addend))
                continue
            low = word(lo[0]) & 0xFFFF
            value = (target + ((insn & 0xFFFF) << 16) + (low - 0x10000 if low & 0x8000 else low)) & 0xFFFFFFFF
            struct.pack_into("<I", data, off, (insn & 0xFFFF0000) | (((value + 0x8000) >> 16) & 0xFFFF))
        elif kind == R_MIPS_LO16:
            low = insn & 0xFFFF
            value = (target + (low - 0x10000 if low & 0x8000 else low)) & 0xFFFF
            struct.pack_into("<I", data, off, (insn & 0xFFFF0000) | value)
        else:
            left.append((off, kind, readonly_name(name), addend))
    return bytes(data), left


def fingerprint(section: dict[str, Any], symbols: dict[str, int] | None = None) -> str:
    data, rows = linked(section, symbols or {})
    digest = hashlib.sha256()
    digest.update(b"nobits" if section["nobits"] else data)
    digest.update(str(section["size"]).encode())
    for row in sorted(rows):
        digest.update(repr(row).encode())
    return f"{section['size']:x}:{digest.hexdigest()[:20]}"


_SYMBOLS: dict[str, dict[str, int]] = {}


def retail_symbols(config: dict[str, Any]) -> dict[str, int]:
    """The main executable's symbols, with the target's own symbol file (an overlay's) on top, and the target's
    read-only data (`.rodata`, `.rdata`) at the address its linked block gives it."""
    files = [str(ctr_match.SYMBOL_FILE.relative_to(ctr_match.ROOT))]
    if config.get("symbol_file") and config["symbol_file"] not in files:
        files.append(config["symbol_file"])
    merged: dict[str, int] = {}
    for name in files:
        if name not in _SYMBOLS:
            _SYMBOLS[name] = ctr_match.load_symbol_file(ctr_match.repository_path(name))
        merged.update(_SYMBOLS[name])
    rodata = (config.get("linked_block") or {}).get("rodata_address")
    if rodata is not None:
        merged[".rodata"] = int(str(rodata), 0)
    return merged


def owned_sections(config: dict[str, Any]) -> dict[str, set[str]] | None:
    """Source -> the sections a resident target compares; None = every allocated section (artifact and data targets)."""
    if config["kind"] != "resident":
        return None
    symbols = ctr_resident.parse_symbols(ctr_match.repository_path(config["symbol_file"]))
    owned: dict[str, set[str]] = {}
    for function in ctr_resident.discover_function_ranges(config, symbols):
        owned.setdefault(function.source, set()).add(f".{function.emitted_name}")
    if config.get("linked_block") or config.get("data_ranges") or config.get("bss_ranges") or config.get("initialized_data"):
        for names in owned.values():
            names.add("*data")
    return owned


def lock_jobs(manifest: dict[str, Any], only: list[str] | None) -> list[tuple[str, str, dict[str, Any]]]:
    jobs = []
    for config in ctr_match.configured_targets(manifest):
        if config["kind"] == "probe" or (only and config["name"] not in only):
            continue
        for source in ctr_match.source_paths(config):
            jobs.append((config["name"], source, config))
    return jobs


def compile_job(job: tuple[str, str, dict[str, Any], ctr_match.Toolchain]) -> tuple[str, str, dict[str, str] | None, str | None]:
    name, source, config, toolchain = job
    path = ctr_match.repository_path(source)
    output = LOCK_BUILD / name / Path(source).with_suffix("") / path.name
    try:
        obj, _deps = ctr_match.build_object(toolchain, config, path, output)
        sections = elf_sections(obj.read_bytes())
    except (ctr_match.MatchError, OSError, ValueError) as exc:
        return name, source, None, str(exc).strip().splitlines()[-1] if str(exc).strip() else repr(exc)
    owned = owned_sections(config)
    symbols = retail_symbols(config)
    keep: dict[str, str] = {}
    want = None if owned is None else owned.get(source, set())
    for section, body in sections.items():
        is_data = section in DATA_SECTIONS or any(section.startswith(d + ".") for d in DATA_SECTIONS)
        if want is None or section in want or (is_data and "*data" in want):
            key = readonly_name(section)
            keep[key if key not in keep else section] = fingerprint(body, symbols)
    return name, source, keep, None


DATA_SECTIONS = {".rodata", ".data", ".sdata", ".sbss", ".bss", ".rdata", ".lit4", ".lit8"}


def build_lock(manifest: dict[str, Any], only: list[str] | None, jobs_n: int) -> tuple[dict[str, Any], list[str]]:
    toolchain = ctr_match.resolve_toolchain(manifest)
    jobs = [(*job, toolchain) for job in lock_jobs(manifest, only)]
    lock: dict[str, dict[str, dict[str, str]]] = {}
    errors: list[str] = []
    with ProcessPoolExecutor(max_workers=jobs_n) as pool:
        for name, source, keep, error in pool.map(compile_job, jobs, chunksize=4):
            if error is not None:
                errors.append(f"{name}: {source}: {error}")
                continue
            if keep:
                lock.setdefault(name, {})[source] = dict(sorted(keep.items()))
    return {k: dict(sorted(v.items())) for k, v in sorted(lock.items())}, errors


def compare(old: dict[str, Any], new: dict[str, Any]) -> list[str]:
    lines = []
    for target in sorted(set(old) | set(new)):
        a, b = old.get(target, {}), new.get(target, {})
        for source in sorted(set(a) | set(b)):
            sa, sb = a.get(source, {}), b.get(source, {})
            for section in sorted(set(sa) | set(sb)):
                if section not in sb:
                    lines.append(f"GONE     {target}: {source} {section}")
                elif section not in sa:
                    lines.append(f"NEW      {target}: {source} {section}")
                elif sa[section] != sb[section]:
                    lines.append(f"CHANGED  {target}: {source} {section} ({sa[section]} -> {sb[section]})")
    return lines


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("command", choices=("check", "update"))
    parser.add_argument("--manifest", default=str(ctr_match.DEFAULT_MANIFEST))
    parser.add_argument("--lock", default=str(LOCK_FILE))
    parser.add_argument("--target", action="append", help="only this target (repeatable)")
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    args = parser.parse_args(argv)

    manifest = ctr_match.load_json(Path(args.manifest))
    lock_path = Path(args.lock)
    try:
        new, errors = build_lock(manifest, args.target, args.jobs)
    except ctr_match.MatchError as exc:
        print(f"lock: {exc}", file=sys.stderr)
        return 2
    for line in errors:
        print(f"ERROR    {line}")
    count = sum(len(s) for t in new.values() for s in t.values())

    if args.command == "update":
        if errors:
            print("lock: not written, a source did not compile", file=sys.stderr)
            return 1
        data = {"schema": LOCK_SCHEMA, "toolchain": manifest["toolchain"]["compiler"]["version"], "targets": new}
        if args.target and lock_path.exists():
            old = json.loads(lock_path.read_text())
            merged = dict(old.get("targets", {}))
            for target in args.target:
                merged.pop(target, None)
            merged.update(new)
            data["targets"] = dict(sorted(merged.items()))
        lock_path.write_text(json.dumps(data, indent=1, sort_keys=False) + "\n")
        print(f"lock: {count} sections from {len(new)} targets written to {lock_path.relative_to(ctr_match.ROOT)}")
        return 0

    if not lock_path.exists():
        print(f"lock: {lock_path} is missing; run `lock.py update`", file=sys.stderr)
        return 1
    old = json.loads(lock_path.read_text()).get("targets", {})
    if args.target:
        old = {k: v for k, v in old.items() if k in args.target}
    lines = compare(old, new)
    for line in lines:
        print(line)
    if lines or errors:
        print(f"lock: {len(lines)} section(s) differ from the lock, {len(errors)} compile error(s). "
              "A deliberate change: verify it on the retail exe (match.sh check), then `lock.py update`.")
        return 1
    print(f"lock: {count} sections in {len(new)} targets are as locked")
    return 0


if __name__ == "__main__":
    sys.exit(main())
