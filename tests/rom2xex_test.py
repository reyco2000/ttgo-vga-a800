#!/usr/bin/env python3
import importlib.util
import os
import struct
import tempfile
import unittest


ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SPEC = importlib.util.spec_from_file_location("rom2xex", os.path.join(ROOT, "rom2xex.py"))
ROM2XEX = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(ROM2XEX)


def blocks(image):
    result = []
    pos = 2
    while pos < len(image):
        start, end = struct.unpack_from("<HH", image, pos)
        pos += 4
        data = image[pos : pos + end - start + 1]
        pos += len(data)
        result.append((start, end, data))
    return result


class Rom2XexTest(unittest.TestCase):
    def convert(self, name, size, **options):
        with tempfile.TemporaryDirectory() as directory:
            path = os.path.join(directory, name)
            with open(path, "wb") as output:
                output.write(bytes((i & 255 for i in range(size))))
            return ROM2XEX.convert(path, **options)

    def test_xl_layout_and_omissions(self):
        full = blocks(self.convert("ATARIXL.ROM", 0x4000))
        self.assertEqual([(a, b) for a, b, _ in full],
                         [(0, 15), (0xC000, 0xCFFF), (0x5000, 0x57FF),
                          (0xD800, 0xDFFF), (0xE000, 0xFFFF)])
        small = blocks(self.convert("ATARIXL.ROM", 0x4000,
                                    no_self_test=True,
                                    no_floating_point=True))
        self.assertEqual([(a, b) for a, b, _ in small],
                         [(0, 15), (0xC000, 0xCFFF), (0xE000, 0xFFFF)])

    def test_osa_and_5200_layouts(self):
        osa = blocks(self.convert("ATARIOSA.ROM", 0x2800))
        self.assertEqual([(a, b) for a, b, _ in osa],
                         [(0, 15), (0xD800, 0xDFFF), (0xE000, 0xFFFF)])
        console = blocks(self.convert("5200.ROM", 0x800))
        self.assertEqual([(a, b) for a, b, _ in console],
                         [(0, 15), (0xF800, 0xFFFF)])

    def test_inapplicable_omission_is_rejected(self):
        with self.assertRaises(ValueError):
            self.convert("ATARIOSA.ROM", 0x2800, no_self_test=True)
        with self.assertRaises(ValueError):
            self.convert("5200.ROM", 0x800, no_floating_point=True)


if __name__ == "__main__":
    unittest.main()
