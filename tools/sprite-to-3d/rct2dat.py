# Copyright (c) 2026 OpenRCT2 developers
# Licensed under the GNU General Public License version 3.

"""Read RCT2 object DATs and g1.dat without writing anything.

Sawyer chunk encodings, the G1 image table, and the default palette sprite
(g1 entry 1532, SPR_GAME_PALETTE_DEFAULT) follow the layouts OpenRCT2 uses.
This module does not bundle any game data. Callers pass paths at runtime.
"""

from __future__ import annotations

import os
import re
import struct

import numpy as np
from PIL import Image

# OpenRCT2 SpriteIds.h: SPR_GAME_PALETTE_DEFAULT.
PALETTE_G1_INDEX = 1532

SMALL_SCENERY = 1
LARGE_SCENERY = 2
WALL = 3
BANNER = 4
FOOTPATH = 5
PATH_ADDITION = 6
SCENERY_GROUP = 7
PARK_ENTRANCE = 8

TYPE_NAMES = {
    SMALL_SCENERY: "small_scenery",
    LARGE_SCENERY: "large_scenery",
    WALL: "walls",
    BANNER: "banners",
    FOOTPATH: "footpaths",
    PATH_ADDITION: "path_additions",
    SCENERY_GROUP: "scenery_groups",
    PARK_ENTRANCE: "park_entrances",
}


def ror8(value, count):
    count &= 7
    return ((value >> count) | (value << (8 - count))) & 0xFF


# A scenery object chunk is far smaller than this. The cap stops a corrupt
# length from growing without bound.
_MAX_DECODED = 32 * 1024 * 1024


def _need(src, index, what):
    if index < 0 or index >= len(src):
        raise ValueError(f"truncated {what}: need offset {index}, length {len(src)}")


def rle_decode(src):
    out = bytearray()
    i = 0
    n = len(src)
    while i < n:
        b = src[i]
        i += 1
        if b & 0x80:
            count = 257 - b
            _need(src, i, "RLE run")
            if len(out) + count > _MAX_DECODED:
                raise ValueError("RLE output exceeds the size cap")
            out += bytes([src[i]]) * count
            i += 1
        else:
            count = b + 1
            if i + count > n:
                raise ValueError(f"truncated RLE literal: need {count} bytes at {i}, length {n}")
            if len(out) + count > _MAX_DECODED:
                raise ValueError("RLE output exceeds the size cap")
            out += src[i : i + count]
            i += count
    return bytes(out)


def decompress(src):
    out = bytearray()
    i = 0
    while i < len(src):
        b = src[i]
        i += 1
        if b == 0xFF:
            _need(src, i, "literal")
            if len(out) >= _MAX_DECODED:
                raise ValueError("decompressed output exceeds the size cap")
            out.append(src[i])
            i += 1
        else:
            off = (b >> 3) - 32
            count = (b & 7) + 1
            start = len(out) + off
            if start < 0 or start + count > len(out):
                raise ValueError(
                    f"decompression reference starts at {start} for {count} bytes, output is {len(out)}"
                )
            if len(out) + count > _MAX_DECODED:
                raise ValueError("decompressed output exceeds the size cap")
            for k in range(count):
                out.append(out[start + k])
    return bytes(out)


def rotate_bytes(src):
    out = bytearray(len(src))
    code = 1
    for i, b in enumerate(src):
        out[i] = ror8(b, code)
        code = (code + 2) & 7
    return bytes(out)


def read_chunk(data, pos):
    """Decode one Sawyer chunk. Encoding byte, then a little-endian length."""
    if pos < 0 or pos + 5 > len(data):
        raise ValueError(f"truncated Sawyer chunk header at {pos}, file is {len(data)} bytes")
    encoding = data[pos]
    length = struct.unpack_from("<I", data, pos + 1)[0]
    if length > _MAX_DECODED or pos + 5 + length > len(data):
        raise ValueError(
            f"truncated Sawyer chunk: encoding {encoding}, length {length}, file has {len(data) - (pos + 5)} bytes left"
        )
    raw = data[pos + 5 : pos + 5 + length]
    if encoding == 0:
        return raw
    if encoding == 1:
        return rle_decode(raw)
    if encoding == 2:
        return decompress(rle_decode(raw))
    if encoding == 3:
        return rotate_bytes(raw)
    raise ValueError(f"unknown Sawyer encoding {encoding}")


def read_strings(data, pos):
    strings = {}
    while True:
        lang = data[pos]
        pos += 1
        if lang == 0xFF:
            return strings, pos
        end = data.index(b"\0", pos)
        strings[lang] = data[pos:end].decode("latin-1")
        pos = end + 1


def read_image_table(data, pos):
    count, size = struct.unpack_from("<II", data, pos)
    pos += 8
    entries = [struct.unpack_from("<IhhhhHH", data, pos + 16 * i) for i in range(count)]
    base = pos + 16 * count
    return entries, data[base : base + size]


def parse_object(path):
    """Return (type, name, source, strings, g1_entries, image_bytes)."""
    data = open(path, "rb").read()
    flags = struct.unpack_from("<I", data, 0)[0]
    name = data[4:12].decode("latin-1")
    obj_type = flags & 0x0F
    source = (flags >> 4) & 0x0F
    chunk = read_chunk(data, 16)
    fixed = {
        1: 0x1C,
        2: 0x1A,
        3: 0x0E,
        4: 0x0C,
        5: 0x0E,
        6: 0x0E,
        7: 0x10E,
        8: 0x08,
    }.get(obj_type)
    if fixed is None:
        return obj_type, name, source, None, None, None
    pos = fixed
    strings, pos = read_strings(chunk, pos)
    if obj_type in (1, 2, 3, 4, 6):
        pos += 16  # scenery group entry
    if obj_type == 1:
        if len(chunk) < 10:
            raise ValueError("truncated small-scenery header")
        sflags = struct.unpack_from("<I", chunk, 6)[0]
        if sflags & (1 << 15):  # hasFrameOffsets
            while True:
                if pos >= len(chunk):
                    raise ValueError("truncated frame-offset list")
                if chunk[pos] == 0xFF:
                    break
                pos += 1
            pos += 1
    elif obj_type == 2:
        if len(chunk) < 8:
            raise ValueError("truncated large-scenery header")
        if chunk[7] & (1 << 2):  # is3DText
            pos += 0x40E
        while True:
            if pos + 2 > len(chunk):
                raise ValueError("truncated large-scenery tile list")
            if struct.unpack_from("<H", chunk, pos)[0] == 0xFFFF:
                break
            if pos + 9 > len(chunk):
                raise ValueError("truncated large-scenery tile")
            pos += 9
        pos += 2
    elif obj_type == 7:
        while True:
            if pos >= len(chunk):
                raise ValueError("truncated scenery-group list")
            if chunk[pos] == 0xFF:
                break
            if pos + 16 > len(chunk):
                raise ValueError("truncated scenery-group entry")
            pos += 16
        pos += 1
    entries, image_bytes = read_image_table(chunk, pos)
    return obj_type, name, source, strings, entries, image_bytes


def decode_image(entry, image_bytes, palette, want_indexed=False):
    """Decode one G1 element to RGBA. Index 0 is transparent on raw bitmaps.

    RLE rows (flag bit 2) use the standard scan-element encoding. Palette
    entries (flag bit 3) and empty images return None.
    """
    offset, width, height, _x_off, _y_off, flags, _zoom = entry
    if flags & 8 or width <= 0 or height <= 0:
        return None
    indices = np.zeros((height, width), np.uint8)
    alpha = np.zeros((height, width), np.uint8)
    if flags & 4:
        if offset < 0 or offset + 2 * height > len(image_bytes):
            raise ValueError("truncated RLE row table")
        for y in range(height):
            q = struct.unpack_from("<H", image_bytes, offset + 2 * y)[0] + offset
            while True:
                if q + 2 > len(image_bytes):
                    raise ValueError("truncated RLE scan element")
                b1 = image_bytes[q]
                x = image_bytes[q + 1]
                q += 2
                run = b1 & 0x7F
                if run:
                    if x + run > width or q + run > len(image_bytes):
                        raise ValueError("RLE run leaves the sprite")
                    indices[y, x : x + run] = np.frombuffer(image_bytes, np.uint8, run, q)
                    alpha[y, x : x + run] = 255
                q += run
                if b1 & 0x80:
                    break
    else:
        nbytes = width * height
        if offset < 0 or offset + nbytes > len(image_bytes):
            raise ValueError("truncated raw bitmap")
        indices[:] = np.frombuffer(image_bytes, np.uint8, nbytes, offset).reshape(height, width)
        alpha[indices != 0] = 255
    rgba = np.zeros((height, width, 4), np.uint8)
    rgba[..., :3] = palette[indices]
    rgba[..., 3] = alpha
    colour = Image.fromarray(rgba, "RGBA")
    if not want_indexed:
        return colour
    indexed = indices.copy()
    indexed[alpha == 0] = 0
    paletted = Image.fromarray(indexed, "P")
    paletted.putpalette(palette.reshape(-1).tolist())
    paletted.info["transparency"] = 0
    return colour, paletted


def load_palette(g1_path):
    """Load the 256 RGB entries from g1.dat's default palette sprite.

    Bytes in the file are BGR. Indices 243-254 are the primary recolour
    range; the base palette leaves most of that range black because the
    game overwrites it at runtime. Those slots are filled with the grey
    ramp at 10-21 so a recolourable object stays visible when decoded.
    """
    data = open(g1_path, "rb").read()
    count, _size = struct.unpack_from("<II", data, 0)
    base = 8 + 16 * count
    offset, width, _height, x_off, _y_off, flags, _zoom = struct.unpack_from(
        "<IhhhhHH", data, 8 + 16 * PALETTE_G1_INDEX
    )
    if not flags & 8:
        raise ValueError(f"g1 entry {PALETTE_G1_INDEX} is not a palette (flags {flags:#x})")
    palette = np.zeros((256, 3), np.uint8)
    for i in range(width):
        b, g, r = data[base + offset + 3 * i : base + offset + 3 * i + 3]
        palette[x_off + i] = (r, g, b)
    palette[243:255] = palette[10:22]
    return palette


def load_standard_palette(header_path=None):
    """RGB triples from ImageImporter.h StandardPalette (stored there as B, G, R, A).

    That table is the game palette already in this repository. It is the same
    palette Data/g1.dat carries, so an atlas painted with it matches a g1.dat build.
    """
    if header_path is None:
        here = os.path.dirname(os.path.abspath(__file__))
        header_path = os.path.join(here, "..", "..", "src", "openrct2", "drawing", "ImageImporter.h")
    text = open(header_path, encoding="utf-8").read()
    marker = text.find("StandardPalette")
    if marker < 0:
        raise ValueError(f"StandardPalette not found in {header_path}")
    body = text[marker : text.find("};", marker)]
    found = re.findall(r"\{\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*\d+\s*\}", body)
    if len(found) < 256:
        raise ValueError(f"StandardPalette has {len(found)} colours, expected 256")
    palette = np.zeros((256, 3), np.uint8)
    for i, (b, g, r) in enumerate(found[:256]):
        palette[i] = (int(r), int(g), int(b))
    return palette


def load_palette_file(path, index_base=0):
    """Load a palette the caller already has on disk.

    Accepts an indexed PNG, a 768-byte RGB file, or a text file of RRGGBB
    hex values. index_base is the palette index of the first entry (a hex
    list that starts at index 10 uses index_base 10). Missing entries stay
    black. This does not ship a palette; it only reads one.
    """
    palette = np.zeros((256, 3), np.uint8)
    lower = path.lower()
    if lower.endswith(".png"):
        image = Image.open(path)
        raw = image.getpalette()
        if not raw:
            raise ValueError(f"{path} has no palette; pass Data/g1.dat or an indexed PNG")
        colours = np.array(raw, np.uint8).reshape(-1, 3)
        n = min(len(colours), 256 - index_base)
        palette[index_base : index_base + n] = colours[:n]
        return palette
    blob = open(path, "rb").read()
    if lower.endswith(".raw") or (len(blob) in (768, 768 - index_base * 3) and lower.endswith(".bin")):
        colours = np.frombuffer(blob, np.uint8).reshape(-1, 3)
        n = min(len(colours), 256 - index_base)
        palette[index_base : index_base + n] = colours[:n]
        return palette
    text = blob.decode("ascii", errors="ignore")
    tokens = []
    for line in text.splitlines():
        line = line.strip()
        if not line or line.startswith("#") or line.startswith("GIMP") or line.startswith("Name:"):
            continue
        if line.startswith("Columns:") or line.startswith("Channels:") or line.startswith("#"):
            continue
        # GIMP .gpl: "R G B name"
        parts = line.replace(",", " ").split()
        if len(parts) >= 3 and all(p.lstrip("-").isdigit() for p in parts[:3]):
            tokens.append((int(parts[0]), int(parts[1]), int(parts[2])))
            continue
        hex_token = line.split()[0].lstrip("#")
        if len(hex_token) == 6:
            tokens.append(tuple(int(hex_token[i : i + 2], 16) for i in (0, 2, 4)))
    if not tokens:
        raise ValueError(f"no colours found in {path}")
    n = min(len(tokens), 256 - index_base)
    palette[index_base : index_base + n] = np.array(tokens[:n], np.uint8)
    return palette
