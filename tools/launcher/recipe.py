#!/usr/bin/env python3
"""The game's build as a fixed list of compiler steps, for the compiler the CTR Launcher carries.

The launcher package holds no CMake and no Ninja. Play runs tools/launcher/build-recipe.json (every compile, the
SDL archive and the link, exactly as CMake and Ninja run them) with compiler/: an llvm-mingw cut down to the files
this port's build reads and the programs that read them. This script writes that file and cuts that compiler.

    python tools/launcher/recipe.py generate --toolchain DIR   write build-recipe.json from CMakeLists.txt
    python tools/launcher/recipe.py check --toolchain DIR      fail when build-recipe.json no longer matches it
    python tools/launcher/recipe.py run --toolchain DIR --build DIR [--jobs N] [--used FILE]
        build the game from it; --used writes every toolchain file the build read (JSON)
    python tools/launcher/recipe.py compiler --from DIR --used FILE --out DIR
        copy only those files, and the programs and DLLs that read them, out of an llvm-mingw

DIR for generate, check and run is an llvm-mingw for this machine (Linux, macOS or Windows). generate and check
need CMake and Ninja too (a developer's, never the player's). The steps hold {src}, {build} and {build_id} where
this machine's paths and commit were, so the file is the same whichever machine wrote it.
"""

from __future__ import annotations

import argparse
import concurrent.futures
import difflib
import hashlib
import json
import os
import re
import shlex
import shutil
import struct
import subprocess
import sys
import tarfile
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
RECIPE = Path(__file__).with_name("build-recipe.json")
TARGET = "i686-w64-mingw32"
BUILD_TARGET = "ctr_native"
FORMAT = 1

# What llvm-mingw's i686-w64-mingw32-clang wrapper and its .cfg files add, spelled out so the compiler needs
# neither: the same flags on every host.
CLANG = ["--no-default-config", "--start-no-unused-arguments", f"--target={TARGET}", "-rtlib=compiler-rt",
         "-unwindlib=libunwind", "-stdlib=libc++", "-fuse-ld=lld", "--end-no-unused-arguments"]
DRIVERS = {
    "cc": ["clang"] + CLANG,
    "cxx": ["clang", "--driver-mode=g++"] + CLANG,
    "ar": ["llvm-ar"],
    "ranlib": ["llvm-ar", "s"],
}
# The programs the drivers run: clang starts ld.lld itself to link.
PROGRAMS = ["clang", "ld.lld", "llvm-ar"]
# A Windows command line holds 32,767 characters; past this a step's arguments go in a response file.
RSP_OVER = 8000


def exe(name: str) -> str:
    return name + ".exe" if os.name == "nt" else name


# --------------------------------------------------------------------------------------------- generate


def wrapper(token: str, toolchain: Path) -> str | None:
    """The driver a command of CMake's starts with: i686-w64-mingw32-clang -> cc, ...-llvm-ar -> ar."""
    name = Path(token).name.lower()
    if name.endswith(".exe"):
        name = name[:-4]
    if not Path(token).resolve().is_relative_to(toolchain.resolve()) and not Path(token).is_relative_to(toolchain):
        return None
    for suffix, kind in [("clang++", "cxx"), ("clang", "cc"), ("llvm-ar", "ar"), ("ar", "ar"),
                         ("llvm-ranlib", "ranlib"), ("ranlib", "ranlib")]:
        if name == suffix or name.endswith("-" + suffix):
            return kind
    return None


def template(token: str, src: Path, build: Path, build_id: str) -> str:
    for path, name in [(build, "{build}"), (src, "{src}")]:
        for spelling in {str(path), path.as_posix()}:
            token = token.replace(spelling, name)
    if build_id and build_id != "unknown":
        token = token.replace(build_id, "{build_id}")
    return token


def cmake() -> str:
    """The CMake that writes the recipe: CTR_CMAKE, else PATH's (CI names the runner's packaged one)."""
    return os.environ.get("CTR_CMAKE") or "cmake"


def sdl_revision() -> str:
    """SDL's revision string, fixed. Left to SDL, it is `git describe` of this repo (externals/SDL is vendored, not a
    submodule), so every commit would change SDL_revision.h and the recipe with it."""
    text = (ROOT / "externals/SDL/include/SDL3/SDL_version.h").read_text(encoding="utf-8")
    parts = [re.search(rf"#define SDL_{k}_VERSION\s+(\d+)", text) for k in ("MAJOR", "MINOR", "MICRO")]
    version = ".".join(m.group(1) for m in parts) if all(parts) else "3"
    return f"SDL-{version}-ctr-native"


def configure(toolchain: Path, build: Path) -> None:
    bin_dir = toolchain / "bin"
    cmd = [cmake(), "-S", str(ROOT), "-B", str(build), "-G", "Ninja", f"-DSDL_REVISION={sdl_revision()}",
           f"-DCMAKE_C_COMPILER={(bin_dir / exe(f'{TARGET}-clang')).as_posix()}",
           f"-DCMAKE_CXX_COMPILER={(bin_dir / exe(f'{TARGET}-clang++')).as_posix()}",
           f"-DCMAKE_RC_COMPILER={(bin_dir / exe(f'{TARGET}-windres')).as_posix()}",
           "-DCMAKE_BUILD_TYPE=Release", "-DBUILD_TESTING=OFF"]
    if os.name != "nt":
        cmd.append("-DCMAKE_SYSTEM_NAME=Windows")
    subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL)


def split_command(line: str) -> list[list[str]]:
    """One line of `ninja -t commands` -> its commands (`a && b`), the shell's no-op `:` left out."""
    if os.name == "nt":
        raise SystemExit("generate reads POSIX shell commands: run it on Linux, macOS or WSL")
    out: list[list[str]] = [[]]
    for token in shlex.split(line):
        if token == "&&":
            out.append([])
        else:
            out[-1].append(token)
    return [c for c in out if c and c != [":"]]


def cmake_version() -> str:
    out = subprocess.run([cmake(), "--version"], capture_output=True, text=True).stdout
    m = re.search(r"cmake version (\S+)", out)
    return m.group(1) if m else "unknown"


def generate_recipe(toolchain: Path) -> dict:
    with tempfile.TemporaryDirectory(prefix="ctr-recipe-") as tmp:
        build = Path(tmp) / "build"
        configure(toolchain, build)
        cache = (build / "CMakeCache.txt").read_text(encoding="utf-8", errors="replace")
        m = re.search(r"CTR_NATIVE_BUILD_ID=\\?\"([^\"\\]+)", (build / "build.ninja").read_text(encoding="utf-8"))
        build_id = m.group(1) if m else "unknown"
        lines = subprocess.run(["ninja", "-C", str(build), "-t", "commands", BUILD_TARGET], check=True,
                               capture_output=True, text=True).stdout.splitlines()
        steps: list[dict] = []
        flag_sets: list[list[str]] = []
        produced: set[str] = set()
        for line in lines:
            ops: list[list[str]] = []
            out = None
            for cmd in split_command(line):
                if Path(cmd[0]).name == "cmake" and cmd[1:3] == ["-E", "rm"]:
                    ops.append(["{rm}"] + [template(t, ROOT, build, build_id) for t in cmd[3:] if t != "-f"])
                    continue
                kind = wrapper(cmd[0], toolchain)
                if kind is None:
                    raise SystemExit(f"a command the recipe can't run: {cmd[0]}\n  {line[:300]}")
                args = [template(t, ROOT, build, build_id) for t in cmd[1:]]
                ops.append(["{" + kind + "}"] + args)
                if kind in ("cc", "cxx") and "-o" in args:
                    out = args[args.index("-o") + 1]
                elif kind == "ar":
                    out = next(a for a in args[1:] if not a.startswith("-"))
            if out is None:
                raise SystemExit(f"a step with no output: {line[:300]}")
            step: dict = {"out": out}
            first = ops[0]
            compile_ = len(ops) == 1 and first[0] in ("{cc}", "{cxx}") and "-c" in first
            if compile_:
                args = first[1:]
                tail = {}
                rest = []
                i = 0
                while i < len(args):
                    if args[i] in ("-MT", "-MF", "-o", "-c") and i + 1 < len(args):
                        tail[args[i]] = args[i + 1]
                        i += 2
                    elif args[i] == "-MD":
                        i += 1
                    else:
                        rest.append(args[i])
                        i += 1
                if tail.get("-MT", tail["-o"]) != tail["-o"]:
                    raise SystemExit(f"-MT is not the object: {line[:300]}")
                if rest not in flag_sets:
                    flag_sets.append(rest)
                step.update({"cc": first[0][1:-1], "flags": flag_sets.index(rest), "src": tail["-c"]})
                if "-MF" in tail:
                    step["dep"] = tail["-MF"]
            else:
                step["run"] = ops
            steps.append(step)
            produced.add(out)
        # Stages: a step runs after every step whose output it names (the PCH, objects, the archive).
        stage_of: dict[str, int] = {}
        for step in steps:
            words = [step.get("src", "")] + [w for op in step.get("run", []) for w in op]
            if "flags" in step:
                words += flag_sets[step["flags"]]
            needs = [stage_of[o] for o in stage_of if any(o in w for w in words)]
            step["stage"] = 1 + max(needs) if needs else 0
            stage_of[step["out"]] = step["stage"]
        # Files configure wrote that the steps read (SDL's build config, the PCH headers).
        generated = {}
        for path in sorted(build.rglob("*")):
            rel = path.relative_to(build).as_posix()
            if not path.is_file() or rel in produced or rel.startswith(("CMakeFiles/", ".cmake/")) or "/CMakeTmp/" in rel:
                continue
            if path.suffix not in (".h", ".hxx", ".c", ".cxx"):
                continue
            generated[rel] = template(path.read_text(encoding="utf-8"), ROOT, build, build_id)
        version = re.search(r"CMAKE_PROJECT_VERSION:STATIC=(\S*)", cache)
    return {
        "format": FORMAT,
        "about": "Written by tools/launcher/recipe.py generate from CMakeLists.txt (Release, BUILD_TESTING=OFF). "
                 "Run in the build folder: {src} = the source, {build} = the build folder, {build_id} = the "
                 "source's BUILD_ID. Steps of one stage can run at once.",
        "target": TARGET,
        "cmake": cmake_version(),
        "project_version": version.group(1) if version else "",
        "drivers": DRIVERS,
        "programs": PROGRAMS,
        "exe": "ctr_native.exe",
        "generated": generated,
        "flags": flag_sets,
        "steps": steps,
    }


def dump(recipe: dict) -> str:
    return json.dumps(recipe, indent=1, sort_keys=False) + "\n"


def recipe_diff(have: str, want: str, limit: int) -> list[str]:
    """What changed between two recipe texts, short: a generated file is compared line by line, any line is cut."""
    def lines(text: str) -> list[str]:
        try:
            recipe = json.loads(text) if text else {}
        except ValueError:
            return text.splitlines()
        generated = recipe.pop("generated", {}) if isinstance(recipe, dict) else {}
        out = dump(recipe).splitlines()
        for name, body in sorted(generated.items()):
            out += [f"{name}: {line}" for line in str(body).splitlines()]
        return out
    diff = [d if len(d) <= 200 else d[:197] + "..." for d in
            difflib.unified_diff(lines(have), lines(want), "committed", "from CMakeLists.txt", n=0, lineterm="")]
    return diff[:limit] + ([f"... {len(diff) - limit} more diff lines"] if len(diff) > limit else [])


# --------------------------------------------------------------------------------------------- run


def load(path: Path = RECIPE) -> dict:
    recipe = json.loads(path.read_text(encoding="utf-8"))
    if recipe.get("format") != FORMAT:
        raise SystemExit(f"{path} is format {recipe.get('format')}, this script reads {FORMAT}")
    return recipe


def read_build_id(src: Path) -> str:
    f = src / "BUILD_ID"
    if f.is_file():
        return f.read_text(encoding="utf-8").strip() or "unknown"
    try:
        h = subprocess.run(["git", "rev-parse", "--short=12", "HEAD"], cwd=src, capture_output=True, text=True,
                           check=True).stdout.strip()
        dirty = subprocess.run(["git", "diff", "--quiet"], cwd=src).returncode != 0
        return h + ("-dirty" if dirty else "")
    except (OSError, subprocess.CalledProcessError):
        return "unknown"


def fill(token: str, values: dict[str, str]) -> str:
    for k, v in values.items():
        token = token.replace("{" + k + "}", v)
    return token


def commands(recipe: dict, step: dict, bin_dir: Path, values: dict[str, str]) -> list[list[str]]:
    def driver(kind: str) -> list[str]:
        d = list(recipe["drivers"][kind])
        return [str(bin_dir / exe(d[0]))] + d[1:]

    if "flags" in step:
        args = [fill(t, values) for t in recipe["flags"][step["flags"]]]
        tail = []
        if "dep" in step:
            tail += ["-MD", "-MT", step["out"], "-MF", step["dep"]]
        tail += ["-o", step["out"], "-c", fill(step["src"], values)]
        return [driver(step["cc"]) + args + tail]
    out = []
    for op in step["run"]:
        head, args = op[0], [fill(t, values) for t in op[1:]]
        if head == "{rm}":
            out.append(["{rm}"] + args)
        else:
            out.append(driver(head.strip("{}")) + args)
    return out


def quote_rsp(arg: str) -> str:
    """One argument in a response file, read with --rsp-quoting=posix (the same on every host)."""
    return '"' + arg.replace("\\", "\\\\").replace('"', '\\"') + '"'


def deps_of(depfile: Path) -> list[str]:
    """The files a Make-style depfile (clang -MD) names, after its target."""
    try:
        text = depfile.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return []
    text = text.replace("\\\r\n", " ").replace("\\\n", " ")
    _, _, rest = text.partition(": ")
    words, cur, i = [], "", 0
    while i < len(rest):
        c = rest[i]
        if c == "\\" and i + 1 < len(rest) and rest[i + 1] in " #":
            cur += rest[i + 1]
            i += 2
            continue
        if c in " \t\r\n":
            if cur:
                words.append(cur)
            cur = ""
        else:
            cur += c
        i += 1
    if cur:
        words.append(cur)
    return words


def stamp_of(cmds: list[list[str]]) -> str:
    return hashlib.sha256(json.dumps(cmds).encode("utf-8")).hexdigest()[:16]


def run_recipe(recipe: dict, toolchain: Path, src: Path, build: Path, jobs: int = 0, used: Path | None = None,
               log=print) -> bool:
    """Builds the game in build/ from the recipe. Steps already done (same command, output newer than every file
    it read) are skipped, as Ninja does."""
    build.mkdir(parents=True, exist_ok=True)
    bin_dir = toolchain / "bin"
    values = {"src": src.resolve().as_posix(), "build": build.resolve().as_posix(), "build_id": read_build_id(src)}
    for rel, text in recipe["generated"].items():
        path = build / rel
        text = fill(text, values)
        if not path.is_file() or path.read_text(encoding="utf-8") != text:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text, encoding="utf-8", newline="\n")
    state_file = build / ".ctr-recipe.json"
    try:
        state = json.loads(state_file.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        state = {}
    repro = build / ".ctr-recipe-repro"
    if used is not None:
        shutil.rmtree(repro, ignore_errors=True)
        repro.mkdir()
        state = {}  # measuring reads everything: build every step

    def mtime(p: Path) -> float:
        try:
            return p.stat().st_mtime
        except OSError:
            return -1.0

    def fresh(step: dict, cmds: list[list[str]]) -> bool:
        out = build / step["out"]
        t = mtime(out)
        if t < 0 or state.get(step["out"]) != stamp_of(cmds):
            return False
        inputs = deps_of(build / step["dep"]) if "dep" in step else [
            w for c in cmds for w in c[1:] if not w.startswith("-") and (build / w).is_file()]
        return all(0 <= mtime(build / i) <= t for i in inputs)

    def run_one(n: int, total: int, step: dict, cmds: list[list[str]]) -> tuple[bool, str]:
        text = []
        for rel in [step["out"], step.get("dep")]:
            if rel:
                (build / rel).parent.mkdir(parents=True, exist_ok=True)
        for i, cmd in enumerate(cmds):
            if cmd[0] == "{rm}":
                for p in cmd[1:]:
                    (build / p).unlink(missing_ok=True)
                continue
            if used is not None and "flags" not in step and Path(cmd[0]).stem == "clang":
                cmd = cmd + [f"-Wl,--reproduce={(repro / f'{n}-{i}.tar').as_posix()}"]
            rsp = None
            if sum(len(a) + 1 for a in cmd) > RSP_OVER:
                rsp = build / f".ctr-recipe-{n}-{i}.rsp"
                rsp.write_text("\n".join(quote_rsp(a) for a in cmd[1:]) + "\n", encoding="utf-8")
                cmd = [cmd[0], "--rsp-quoting=posix", "@" + str(rsp)]
            done = subprocess.run(cmd, cwd=build, capture_output=True, text=True, errors="replace")
            if rsp:
                rsp.unlink(missing_ok=True)
            text.append((done.stdout + done.stderr).strip())
            if done.returncode != 0:
                return False, "\n".join(t for t in text if t) + f"\n(exit {done.returncode}: {' '.join(cmd)[:400]})"
        return True, "\n".join(t for t in text if t)

    steps = recipe["steps"]
    total = len(steps)
    jobs = jobs or os.cpu_count() or 4
    n = 0
    ok = True
    for stage in sorted({s["stage"] for s in steps}):
        todo = []
        for step in (s for s in steps if s["stage"] == stage):
            cmds = commands(recipe, step, bin_dir, values)
            if fresh(step, cmds):
                n += 1
                continue
            todo.append((step, cmds))
        with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
            futures = {pool.submit(run_one, i + n + 1, total, s, c): (s, c) for i, (s, c) in enumerate(todo)}
            for fut in concurrent.futures.as_completed(futures):
                step, cmds = futures[fut]
                good, text = fut.result()
                n += 1
                log(f"[{n}/{total}] {'Linking' if 'run' in step else 'Building'} {step['out']}")
                if text:
                    log(text)
                if good:
                    state[step["out"]] = stamp_of(cmds)
                else:
                    ok = False
        state_file.write_text(json.dumps(state, indent=0), encoding="utf-8")
        if not ok:
            return False
    if used is not None:
        used.write_text(json.dumps(measure(recipe, toolchain, build, repro), indent=1) + "\n", encoding="utf-8")
    return True


def measure(recipe: dict, toolchain: Path, build: Path, repro: Path) -> list[str]:
    """Every file of the toolchain the build read: each compile's depfile, each link's reproduce archive."""
    root = toolchain.resolve()
    seen: set[str] = set()

    def add(path: str) -> None:
        p = Path(path)
        if not p.is_absolute():
            return
        for cand in (Path(os.path.normpath(p)), p.resolve()):
            try:
                rel = cand.relative_to(root).as_posix()
            except ValueError:
                continue
            if (root / rel).is_file():
                seen.add(rel)
                return

    for step in recipe["steps"]:
        if "dep" in step:
            for d in deps_of(build / step["dep"]):
                add(d if os.path.isabs(d) else str(build / d))
    for tar in sorted(repro.glob("*.tar")):
        with tarfile.open(tar) as t:
            for name in t.getnames():
                # repro/<absolute path>: the path after the first folder
                parts = name.split("/", 1)
                if len(parts) == 2:
                    rest = parts[1]
                    if os.name == "nt" and re.match(r"^[A-Za-z]/", rest):
                        add(rest[0] + ":/" + rest[2:])  # lld writes C:\x as C/x
                    else:
                        add("/" + rest.lstrip("/"))
    return sorted(seen)


# --------------------------------------------------------------------------------------------- compiler


def pe_headers(data: bytes):
    """A PE image's (machine, image base, data directories offset, sections) or None."""
    if data[:2] != b"MZ" or len(data) < 0x40:
        return None
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe:pe + 4] != b"PE\0\0":
        return None
    machine, nsec = struct.unpack_from("<HH", data, pe + 4)
    optsize = struct.unpack_from("<H", data, pe + 20)[0]
    opt = pe + 24
    magic = struct.unpack_from("<H", data, opt)[0]
    base = struct.unpack_from("<I", data, opt + 28)[0] if magic == 0x10B else struct.unpack_from("<Q", data, opt + 24)[0]
    dd = opt + (96 if magic == 0x10B else 112)
    secs = []
    for i in range(nsec):
        s = opt + optsize + 40 * i
        vsize, va, rsize, raw = struct.unpack_from("<IIII", data, s + 8)
        secs.append((va, max(vsize, rsize), raw))
    return machine, base, dd, secs


def pe_machine(path: Path) -> int | None:
    """The machine a Windows program is built for: 0x14C x86, 0x8664 x64, 0xAA64 arm64."""
    with path.open("rb") as f:
        head = pe_headers(f.read(4096))
    return head[0] if head else None


def pe_imports(path: Path) -> list[str]:
    """The DLL names a Windows program imports: its import directory, then its delay-load one."""
    data = path.read_bytes()
    head = pe_headers(data)
    if head is None:
        return []
    _machine, base, dd, secs = head

    def off(rva: int) -> int | None:
        for va, size, raw in secs:
            if va <= rva < va + size:
                return raw + rva - va
        return None

    def name_at(rva: int) -> str | None:
        o = off(rva)
        if o is None or o >= len(data):
            return None
        end = data.find(b"\0", o)
        return data[o:end if end >= 0 else len(data)].decode("ascii", "replace")

    names = []
    imp_rva = struct.unpack_from("<I", data, dd + 8)[0]
    at = off(imp_rva) if imp_rva else None
    while at is not None and at + 20 <= len(data):
        name_rva = struct.unpack_from("<I", data, at + 12)[0]
        name = name_at(name_rva) if name_rva else None
        if name is None:
            break
        names.append(name)
        at += 20
    delay_rva = struct.unpack_from("<I", data, dd + 13 * 8)[0] if dd + 13 * 8 + 4 <= len(data) else 0
    at = off(delay_rva) if delay_rva else None
    while at is not None and at + 32 <= len(data):
        attrs, name_rva = struct.unpack_from("<II", data, at)
        if name_rva == 0:
            break
        name = name_at(name_rva if attrs & 1 else name_rva - base)  # bit 0 clear: an old table of addresses
        if name is None:
            break
        names.append(name)
        at += 32
    return names


def map_target_file(rel: str, full: Path) -> str | None:
    """A target file measured on one host's llvm-mingw, where the Windows release keeps it."""
    cands = [rel]
    for a, b in [(f"{TARGET}/include/", "include/"), ("generic-w64-mingw32/include/", "include/")]:
        if rel.startswith(a):
            cands.append(b + rel[len(a):])
    return next((c for c in cands if (full / c).is_file()), None)


def make_compiler(full: Path, used: list[str], out: Path, recipe: dict, windows: bool = True) -> dict:
    """Copies, out of a whole llvm-mingw, only what the build read and the programs (with their DLLs or shared
    libraries) that read it. Returns what went in."""
    if out.exists():
        shutil.rmtree(out)
    (out / "bin").mkdir(parents=True)
    files: list[str] = []
    missing: list[str] = []
    for rel in used:
        if rel.startswith("bin/") or rel.startswith("lib/lib") and (".so" in rel or rel.endswith(".dylib")):
            continue  # the programs come below, as this host's or Windows' own
        to = map_target_file(rel, full)
        if to is None:
            missing.append(rel)
            continue
        dst = out / to
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(full / to, dst)
        files.append(to)
    if missing:
        raise SystemExit("not in that llvm-mingw: " + ", ".join(missing[:10]))
    progs = recipe.get("programs", PROGRAMS)
    suffix = ".exe" if windows else ""
    bin_full = full / "bin"
    for prog in progs:
        src = bin_full / (prog + suffix)
        # llvm-mingw's clang(.exe) is a small wrapper; the compiler itself is clang-<major>.
        if prog == "clang":
            real = sorted(bin_full.glob("clang-[0-9]*" + suffix), key=lambda p: len(p.name))
            if real:
                src = real[0]
        if src.is_symlink():
            src = src.resolve()
        if not src.is_file():
            raise SystemExit(f"no {prog} in {bin_full}")
        shutil.copy2(src, out / "bin" / (prog + suffix))
        files.append(f"bin/{prog}{suffix}")
    if windows:
        todo = [out / "bin" / (p + ".exe") for p in progs]
        while todo:
            for dll in pe_imports(todo.pop()):
                have = out / "bin" / dll
                cand = next((p for p in bin_full.iterdir() if p.name.lower() == dll.lower()), None)
                if cand is not None and not have.exists():
                    shutil.copy2(cand, out / "bin" / cand.name)
                    files.append(f"bin/{cand.name}")
                    todo.append(out / "bin" / cand.name)
    else:
        # A Linux or macOS llvm-mingw: its programs find libLLVM and libclang-cpp in ../lib.
        (out / "lib").mkdir(parents=True, exist_ok=True)
        for lib in sorted((full / "lib").iterdir()):
            if not re.match(r"lib(LLVM|clang-cpp)[-.]", lib.name):
                continue
            if lib.is_symlink():
                os.symlink(os.readlink(lib), out / "lib" / lib.name)
            elif lib.is_file():
                shutil.copy2(lib, out / "lib" / lib.name)
                files.append(f"lib/{lib.name}")
    for notice in ["LICENSE.TXT"]:
        if (full / notice).is_file():
            shutil.copy2(full / notice, out / notice)
            files.append(notice)
    size = sum((out / f).stat().st_size for f in files if (out / f).is_file())
    about = {"made_by": "tools/launcher/recipe.py compiler", "from": full.name, "target": TARGET,
             "files": len(files), "bytes": size}
    (out / "COMPILER.json").write_text(json.dumps(about, indent=1) + "\n", encoding="utf-8")
    return about


# --------------------------------------------------------------------------------------------- main


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    g = sub.add_parser("generate")
    g.add_argument("--toolchain", default=os.environ.get("CTR_LLVM_MINGW", ""), required="CTR_LLVM_MINGW" not in os.environ)
    c = sub.add_parser("check")
    c.add_argument("--toolchain", default=os.environ.get("CTR_LLVM_MINGW", ""))
    r = sub.add_parser("run")
    r.add_argument("--toolchain", required=True)
    r.add_argument("--build", required=True)
    r.add_argument("--src", default=str(ROOT))
    r.add_argument("--recipe", default=str(RECIPE))
    r.add_argument("--jobs", type=int, default=0)
    r.add_argument("--used")
    m = sub.add_parser("compiler")
    m.add_argument("--from", dest="full", required=True)
    m.add_argument("--used", required=True)
    m.add_argument("--out", required=True)
    m.add_argument("--host", choices=["windows", "this"], default="windows")
    args = ap.parse_args(argv)

    if args.cmd == "generate":
        text = dump(generate_recipe(Path(args.toolchain)))
        RECIPE.write_text(text, encoding="utf-8", newline="\n")
        recipe = json.loads(text)
        print(f"{RECIPE.relative_to(ROOT)}: {len(recipe['steps'])} steps, {len(recipe['flags'])} flag sets, "
              f"{len(recipe['generated'])} generated files")
        return 0
    if args.cmd == "check":
        if not args.toolchain or not (Path(args.toolchain) / "bin").is_dir():
            print("no llvm-mingw for this machine: set CTR_LLVM_MINGW (or pass --toolchain) to check the recipe")
            return 2
        if not shutil.which(cmake()) or not shutil.which("ninja"):
            print("the recipe check needs cmake (or CTR_CMAKE) and ninja on PATH")
            return 2
        have = RECIPE.read_text(encoding="utf-8") if RECIPE.exists() else ""
        written = json.loads(have).get("cmake") if have else None
        if written and written != cmake_version():
            # Another CMake spells some commands differently: compare where the versions match (CI).
            print(f"the recipe was written with CMake {written}, this is {cmake_version()}: not compared")
            return 2
        want = dump(generate_recipe(Path(args.toolchain)))
        if want != have:
            print(f"{RECIPE.relative_to(ROOT)} is out of date: run `python tools/launcher/recipe.py generate "
                  f"--toolchain DIR` and commit it")
            for line in recipe_diff(have, want, 40):
                print(line)
            return 1
        print(f"{RECIPE.relative_to(ROOT)} is up to date ({len(json.loads(want)['steps'])} steps)")
        return 0
    if args.cmd == "run":
        ok = run_recipe(load(Path(args.recipe)), Path(args.toolchain), Path(args.src), Path(args.build),
                        args.jobs, Path(args.used) if args.used else None)
        return 0 if ok else 1
    if args.cmd == "compiler":
        used = json.loads(Path(args.used).read_text(encoding="utf-8"))
        about = make_compiler(Path(args.full), used, Path(args.out), load(), windows=args.host == "windows")
        print(f"compiler: {about['files']} files, {about['bytes'] / 1e6:.1f} MB in {args.out}")
        return 0
    return 2


if __name__ == "__main__":
    sys.exit(main())
