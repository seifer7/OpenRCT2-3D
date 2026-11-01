# Copyright (c) 2026 OpenRCT2 developers
# Licensed under the GNU General Public License version 3.

"""Bounds checks for the DAT decoder. A truncated buffer must raise ValueError."""

from __future__ import annotations

import struct
import unittest

import rct2dat


class Rct2DatBoundsTest(unittest.TestCase):
    def test_truncated_rle_run(self):
        with self.assertRaises(ValueError):
            rct2dat.rle_decode(b"\x80")

    def test_truncated_rle_literal(self):
        with self.assertRaises(ValueError):
            rct2dat.rle_decode(b"\x05\x01\x02")

    def test_bad_back_reference(self):
        with self.assertRaises(ValueError):
            rct2dat.decompress(b"\x00\x01")

    def test_truncated_literal(self):
        with self.assertRaises(ValueError):
            rct2dat.decompress(b"\xff")

    def test_truncated_chunk_header(self):
        with self.assertRaises(ValueError):
            rct2dat.read_chunk(b"\x00", 0)

    def test_truncated_chunk_body(self):
        header = bytes([1]) + struct.pack("<I", 100)
        with self.assertRaises(ValueError):
            rct2dat.read_chunk(header, 0)

    def test_standard_palette_grey(self):
        palette = rct2dat.load_standard_palette()
        self.assertEqual(palette.shape, (256, 3))
        # Index 10 is the first grey, RGB (23, 35, 35), stored BGR in the header.
        self.assertEqual(tuple(int(c) for c in palette[10]), (23, 35, 35))


if __name__ == "__main__":
    unittest.main()
