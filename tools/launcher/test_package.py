"""Tests for package_windows.py: WebView2 in the package, and the check that nothing outside it is needed."""

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import package_windows as pkg  # noqa: E402
from test_recipe import tiny_pe  # noqa: E402


class PackageTests(unittest.TestCase):
    def test_only_windows_dlls_and_neighbours(self):
        with tempfile.TemporaryDirectory() as d:
            prog = Path(d) / "a.exe"
            prog.write_bytes(tiny_pe(["KERNEL32.dll", "api-ms-win-crt-heap-l1-1-0.dll", "libLLVM-19.dll",
                                      "VCRUNTIME140.dll", "WebView2Loader.dll"]))
            self.assertEqual(pkg.foreign_dlls(prog, {"libllvm-19.dll"}), ["VCRUNTIME140.dll", "WebView2Loader.dll"])

    def test_audit_names_what_is_missing(self):
        with tempfile.TemporaryDirectory() as d:
            out = Path(d)
            (out / "compiler" / "bin").mkdir(parents=True)
            (out / "CTR Launcher.exe").write_bytes(tiny_pe(["USER32.dll", "ole32.dll"], 0x8664))
            (out / "compiler" / "bin" / "clang.exe").write_bytes(tiny_pe(["libLLVM-19.dll", "KERNEL32.dll"], 0x8664))
            (out / "compiler" / "bin" / "libLLVM-19.dll").write_bytes(tiny_pe(["ntdll.dll"], 0x8664))
            game = out / "ctr_native.exe"
            game.write_bytes(tiny_pe(["OPENGL32.dll", "msvcp140.dll"]))
            self.assertEqual(pkg.audit(out, game),
                             ["ctr_native.exe loads msvcp140.dll, which Windows doesn't have and the package doesn't carry"])
            game.write_bytes(tiny_pe(["OPENGL32.dll", "WINMM.DLL"]))
            self.assertEqual(pkg.audit(out, game), [])

    def test_webview2_from_a_folder(self):
        with tempfile.TemporaryDirectory() as d:
            d = Path(d)
            got = d / "download" / "Microsoft.WebView2.FixedVersionRuntime.142.0.3595.94.x64"
            got.mkdir(parents=True)
            (got / "msedgewebview2.exe").write_bytes(tiny_pe(["KERNEL32.dll"], 0x8664))
            (got / "EBWebView").mkdir()
            out = d / "out"
            out.mkdir()
            # The folder itself, or the one holding it (as it unpacks), both work.
            self.assertEqual(pkg.place_webview2(out, str(d / "download"), d / "cache", True), "142.0.3595.94")
            self.assertEqual(pkg.place_webview2(out, str(got), d / "cache", True), "142.0.3595.94")
            self.assertTrue((out / "webview2" / "msedgewebview2.exe").is_file())
            self.assertTrue((out / "webview2" / "EBWebView").is_dir())
            # A later run without --webview2 keeps it.
            self.assertEqual(pkg.place_webview2(out, None, d / "cache", True), "142.0.3595.94")

    def test_webview2_must_be_there_and_x64(self):
        with tempfile.TemporaryDirectory() as d:
            d = Path(d)
            with self.assertRaises(SystemExit) as stop:
                pkg.place_webview2(d / "out", None, d / "cache", True)
            self.assertIn("--no-webview2", str(stop.exception))
            (d / "rt").mkdir()
            (d / "rt" / "msedgewebview2.exe").write_bytes(tiny_pe([], 0x14C))
            with self.assertRaises(SystemExit) as stop:
                pkg.place_webview2(d / "out", str(d / "rt"), d / "cache", True)
            self.assertIn("x64", str(stop.exception))

    def test_package_readme_formats(self):
        text = pkg.README.format(cover=pkg.COVER_NONE, webview2=pkg.WEBVIEW2_GIVEN.format(version="142.0"))
        self.assertIn("webview2\\ holds Microsoft's own copy", text)
        self.assertIn(".iso", text)
        self.assertNotIn("{", text)


if __name__ == "__main__":
    unittest.main()
