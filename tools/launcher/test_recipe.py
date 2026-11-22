"""Tests for tools/launcher/recipe.py that need no compiler (the build itself is proven by package_windows.py)."""

import json
import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import recipe  # noqa: E402


def tiny_pe(dlls: list[str]) -> bytes:
    """A PE32 image with one section holding an import directory that names `dlls`."""
    data = bytearray(0x400)
    data[0:2] = b"MZ"
    struct.pack_into("<I", data, 0x3C, 0x40)
    data[0x40:0x44] = b"PE\0\0"
    struct.pack_into("<HH", data, 0x44, 0x14C, 1)  # machine, one section
    struct.pack_into("<H", data, 0x54, 0xE0)  # optional header size
    opt = 0x58
    struct.pack_into("<H", data, opt, 0x10B)
    struct.pack_into("<II", data, opt + 96 + 8, 0x1000, 20 * (len(dlls) + 1))  # import directory
    sec = opt + 0xE0
    data[sec:sec + 8] = b".idata\0\0"
    struct.pack_into("<IIII", data, sec + 8, 0x200, 0x1000, 0x200, 0x200)
    names = 0x1000 + 20 * (len(dlls) + 1)
    for i, dll in enumerate(dlls):
        struct.pack_into("<I", data, 0x200 + 20 * i + 12, names)
        raw = 0x200 + names - 0x1000
        data[raw:raw + len(dll) + 1] = dll.encode() + b"\0"
        names += len(dll) + 1
    return bytes(data)


class RecipeTests(unittest.TestCase):
    def test_depfiles(self):
        with tempfile.TemporaryDirectory() as d:
            f = Path(d) / "a.d"
            f.write_text("a.obj: /src/a.c /src/my\\ dir/b.h \\\n  C:/llvm/include/stdio.h\n", encoding="utf-8")
            self.assertEqual(recipe.deps_of(f), ["/src/a.c", "/src/my dir/b.h", "C:/llvm/include/stdio.h"])
            self.assertEqual(recipe.deps_of(Path(d) / "none.d"), [])

    def test_response_file_quoting(self):
        self.assertEqual(recipe.quote_rsp("a.obj"), '"a.obj"')
        self.assertEqual(recipe.quote_rsp('C:\\x y\\"q"'), '"C:\\\\x y\\\\\\"q\\""')

    def test_commands(self):
        plan = {
            "drivers": {"cc": ["clang", "--target=i686-w64-mingw32"], "ar": ["llvm-ar"]},
            "flags": [["-I{src}/include", '-DID="{build_id}"']],
        }
        values = {"src": "D:/ctr/source", "build": "D:/ctr/build", "build_id": "abc"}
        bin_dir = Path("bin")
        cmds = recipe.commands(plan, {"out": "a.obj", "cc": "cc", "flags": 0, "src": "{src}/a.c", "dep": "a.d"},
                               bin_dir, values)
        self.assertEqual(cmds[0][1:], ["--target=i686-w64-mingw32", "-ID:/ctr/source/include", '-DID="abc"', "-MD",
                                       "-MT", "a.obj", "-MF", "a.d", "-o", "a.obj", "-c", "D:/ctr/source/a.c"])
        cmds = recipe.commands(plan, {"out": "l.a", "run": [["{rm}", "l.a"], ["{ar}", "qc", "l.a", "a.obj"]]},
                               bin_dir, values)
        self.assertEqual(cmds[0], ["{rm}", "l.a"])
        self.assertEqual(cmds[1][1:], ["qc", "l.a", "a.obj"])

    def test_ninja_lines(self):
        if sys.platform == "win32":
            self.skipTest("generate reads POSIX shell commands")
        line = ": && /cmake -E rm -f l.a && /t/bin/i686-w64-mingw32-llvm-ar qc l.a  a.obj && :"
        self.assertEqual(recipe.split_command(line),
                         [["/cmake", "-E", "rm", "-f", "l.a"], ["/t/bin/i686-w64-mingw32-llvm-ar", "qc", "l.a", "a.obj"]])
        tc = Path("/t")
        self.assertEqual(recipe.wrapper("/t/bin/i686-w64-mingw32-clang++", tc), "cxx")
        self.assertEqual(recipe.wrapper("/t/bin/i686-w64-mingw32-llvm-ranlib", tc), "ranlib")
        self.assertIsNone(recipe.wrapper("/usr/bin/cc", tc))

    def test_windows_layout_of_headers(self):
        with tempfile.TemporaryDirectory() as d:
            full = Path(d)
            (full / "include").mkdir()
            (full / "include" / "stdio.h").write_text("", encoding="utf-8")
            (full / "lib/clang/19/include").mkdir(parents=True)
            (full / "lib/clang/19/include/stddef.h").write_text("", encoding="utf-8")
            self.assertEqual(recipe.map_target_file("i686-w64-mingw32/include/stdio.h", full), "include/stdio.h")
            self.assertEqual(recipe.map_target_file("lib/clang/19/include/stddef.h", full), "lib/clang/19/include/stddef.h")
            self.assertIsNone(recipe.map_target_file("i686-w64-mingw32/include/none.h", full))

    def test_dll_imports(self):
        with tempfile.TemporaryDirectory() as d:
            f = Path(d) / "clang.exe"
            f.write_bytes(tiny_pe(["libLLVM-19.dll", "KERNEL32.dll"]))
            self.assertEqual(recipe.pe_imports(f), ["libLLVM-19.dll", "KERNEL32.dll"])
            f.write_bytes(b"not a program")
            self.assertEqual(recipe.pe_imports(f), [])

    def test_the_committed_recipe_reads(self):
        plan = recipe.load()
        self.assertEqual(plan["exe"], "ctr_native.exe")
        outs = [s["out"] for s in plan["steps"]]
        self.assertEqual(len(outs), len(set(outs)), "two steps write one file")
        self.assertEqual(plan["steps"][-1]["out"], "ctr_native.exe")
        stage = {s["out"]: s["stage"] for s in plan["steps"]}
        for s in plan["steps"]:
            words = [w for op in s.get("run", []) for w in op] + plan["flags"][s["flags"]] if "flags" in s else \
                [w for op in s["run"] for w in op]
            for out, st in stage.items():
                if out != s["out"] and any(out in w for w in words):
                    self.assertLess(st, s["stage"], f"{s['out']} runs before {out}, which it reads")
        json.dumps(plan)


if __name__ == "__main__":
    unittest.main()
