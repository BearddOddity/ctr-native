"""remaining.json and the scorer's targets (no retail exe needed)."""

import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import pipeline  # noqa: E402
import remaining  # noqa: E402
import resident  # noqa: E402


class RemainingTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.manifest = pipeline.load_json(pipeline.DEFAULT_MANIFEST)
        cls.entries = remaining.load_remaining()

    def test_each_function_is_one_retail_span(self):
        symbols = resident.parse_symbols(pipeline.ROOT / "metadata/retail/ntsc-u-926/symbols/syms926.txt")
        order = sorted(symbols, key=lambda symbol: symbol.address)
        follows = {left.name: right.name for left, right in zip(order, order[1:])}
        for entry in self.entries:
            with self.subTest(entry["name"]):
                self.assertEqual(follows[entry["name"]], entry["end_symbol"])
                self.assertGreater(remaining.retail_size(remaining.target_for(self.manifest, entry)), 0)

    def test_no_matching_target_covers_them(self):
        symbols = resident.parse_symbols(pipeline.ROOT / "metadata/retail/ntsc-u-926/symbols/syms926.txt")
        covered = set()
        for target in pipeline.configured_targets(self.manifest, "resident"):
            for symbol, _ in resident.namespace_symbol_ranges(target, symbols):
                covered.add(symbol.name)
        for entry in self.entries:
            self.assertNotIn(entry["name"], covered, "matched: move it out of remaining.json")

    def test_target_takes_the_neighbours_flags(self):
        for entry in self.entries:
            with self.subTest(entry["name"]):
                target = remaining.target_for(self.manifest, entry)
                like = pipeline.target_by_name(self.manifest, entry["like"])
                self.assertEqual(target["compiler_flags"], entry.get("compiler_flags", like["compiler_flags"]))
                self.assertEqual(target["start_symbol"], entry["name"])
                self.assertEqual(target["source"], entry["source"])
                self.assertNotIn(entry["name"], target.get("link_symbols", {}))
                self.assertTrue((pipeline.ROOT / entry["source"]).is_file())
                self.assertEqual(resident.definition_count((pipeline.ROOT / entry["source"]).read_text(), entry["name"]), 1)

    def test_a_bad_file_is_refused(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "remaining.json"
            path.write_text(json.dumps({"functions": [{"name": "x"}, {"name": "x"}]}))
            with self.assertRaises(pipeline.MatchError):
                remaining.load_remaining(path)
            path.write_text(json.dumps({"functions": []}))
            with self.assertRaises(pipeline.MatchError):
                remaining.load_remaining(path)


if __name__ == "__main__":
    unittest.main()
