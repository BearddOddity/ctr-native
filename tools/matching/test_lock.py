from __future__ import annotations

import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import lock  # noqa: E402


def section(words: list[int], relocs: list[tuple[int, int, str, int]]) -> dict:
    data = b"".join(struct.pack("<I", w) for w in words)
    return {"size": len(data), "data": data, "relocs": relocs, "nobits": False}


def words(data: bytes) -> list[int]:
    return [struct.unpack_from("<I", data, i)[0] for i in range(0, len(data), 4)]


SYMBOLS = {"data": 0x800809A0, "func": 0x80012440, "high": 0x80018000}


class LinkedTests(unittest.TestCase):
    def test_applies_relocations_to_retail_addresses(self) -> None:
        sec = section(
            [0x3C020000, 0x8C420010, 0x0C000000, 0x00000000, 0x3C030000, 0x24630000],
            [(0, lock.R_MIPS_HI16, "data", 0), (4, lock.R_MIPS_LO16, "data", 0), (8, lock.R_MIPS_26, "func", 0),
             (12, lock.R_MIPS_32, "data", 0), (16, lock.R_MIPS_HI16, "high", 0), (20, lock.R_MIPS_LO16, "high", 0)],
        )
        data, left = lock.linked(sec, SYMBOLS)
        self.assertEqual(words(data), [0x3C028008, 0x8C4209B0, 0x0C004910, 0x800809A0, 0x3C038002, 0x24638000])
        self.assertEqual(left, [])

    def test_absolute_and_relocated_forms_hash_the_same(self) -> None:
        relocated = section([0x3C020000, 0x8C420010], [(0, lock.R_MIPS_HI16, "data", 0), (4, lock.R_MIPS_LO16, "data", 0)])
        absolute = section([0x3C028008, 0x8C4209B0], [])
        self.assertEqual(lock.fingerprint(relocated, SYMBOLS), lock.fingerprint(absolute, SYMBOLS))

    def test_function_sections_resolve_through_their_function(self) -> None:
        sec = section([0x00000010], [(0, lock.R_MIPS_32, ".func", 0)])
        data, left = lock.linked(sec, SYMBOLS)
        self.assertEqual(words(data), [0x80012450])
        self.assertEqual(left, [])

    def test_unknown_symbols_stay_in_the_hash(self) -> None:
        a = section([0x0C000000], [(0, lock.R_MIPS_26, "unknown_a", 0)])
        b = section([0x0C000000], [(0, lock.R_MIPS_26, "unknown_b", 0)])
        self.assertEqual(lock.linked(a, SYMBOLS)[1], [(0, lock.R_MIPS_26, "unknown_a", 0)])
        self.assertNotEqual(lock.fingerprint(a, SYMBOLS), lock.fingerprint(b, SYMBOLS))

    def test_a_changed_word_changes_the_fingerprint(self) -> None:
        self.assertNotEqual(lock.fingerprint(section([0x24020555], [])), lock.fingerprint(section([0x24020556], [])))


class CompareTests(unittest.TestCase):
    def test_names_changed_new_and_gone_sections(self) -> None:
        old = {"t": {"a.c": {".f": "4:aa", ".g": "4:bb"}}}
        new = {"t": {"a.c": {".f": "4:ab", ".h": "4:cc"}}}
        lines = lock.compare(old, new)
        self.assertEqual([line.split()[0] for line in lines], ["CHANGED", "GONE", "NEW"])

    def test_equal_locks_compare_clean(self) -> None:
        same = {"t": {"a.c": {".f": "4:aa"}}}
        self.assertEqual(lock.compare(same, same), [])


if __name__ == "__main__":
    unittest.main()
