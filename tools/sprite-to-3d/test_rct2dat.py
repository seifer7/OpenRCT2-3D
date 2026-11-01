# Copyright (c) 2026 OpenRCT2 developers
# Licensed under the GNU General Public License version 3.

"""Bounds checks for the DAT decoder. A truncated buffer must raise ValueError."""

from __future__ import annotations

import struct
import sys

import rct2dat


def run():
    try:
        rct2dat.rle_decode(b"\x80")
    except ValueError:
        pass
    else:
        raise SystemExit("truncated RLE run did not raise ValueError")
    try:
        rct2dat.rle_decode(b"\x05\x01\x02")
    except ValueError:
        pass
    else:
        raise SystemExit("truncated RLE literal did not raise ValueError")
    try:
        rct2dat.decompress(b"\x00\x01")
    except ValueError:
        pass
    else:
        raise SystemExit("bad back-reference did not raise ValueError")
    try:
        rct2dat.decompress(b"\xff")
    except ValueError:
        pass
    else:
        raise SystemExit("truncated literal did not raise ValueError")
    # A one-byte file cannot hold a Sawyer chunk header.
    try:
        rct2dat.read_chunk(b"\x00", 0)
    except ValueError:
        pass
    else:
        raise SystemExit("truncated chunk header did not raise ValueError")
    # Header claims 100 bytes that are not in the file.
    header = bytes([1]) + struct.pack("<I", 100)
    try:
        rct2dat.read_chunk(header, 0)
    except ValueError:
        pass
    else:
        raise SystemExit("truncated chunk body did not raise ValueError")
    palette = rct2dat.load_standard_palette()
    if palette.shape != (256, 3):
        raise SystemExit(f"StandardPalette shape {palette.shape}")
    # Index 10 is the first grey, RGB (23, 35, 35), stored BGR in the header.
    if tuple(int(c) for c in palette[10]) != (23, 35, 35):
        raise SystemExit(f"palette[10] is {tuple(palette[10])}")
    print("PASS  rct2dat bounds checks")


if __name__ == "__main__":
    sys.exit(run())
