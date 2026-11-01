# Copyright (c) 2026 OpenRCT2 developers
# Licensed under the GNU General Public License version 3.

"""Extract one RCT2 object's sprites into a local cache folder.

Nothing here is meant to be committed. Point --out at tools/sprite-to-3d/cache
(gitignored) or at any directory outside the repository.

  python3 extract.py --rct2-data-path /path/to/rct2 --dat TCRP --object-id rct2.scenery_small.tcrp --out cache/rct2.scenery_small.tcrp
"""

from __future__ import annotations

import argparse
import json
import os
import sys

import numpy as np

import rct2dat as dat

SMALL_FLAGS = [
    "occupiesFullTile",
    "vOffsetCentre",
    "requiresFlatSurface",
    "isRotatable",
    "isAnimated",
    "canWither",
    "canBeWatered",
    "hasOverlayImage",
    "isDiagonal",
    "hasGlass",
    "hasPrimaryColour",
    "isFountain",
    "isCupidFountain",
    "isClock",
    "isSwampGoo",
    "hasFrameOffsets",
    "flag17",
    "isStackable",
    "prohibitWalls",
    "hasSecondaryColour",
    "hasNoSupports",
    "isVisibleWhenZoomed",
    "isCogwheel",
    "allowSupportsAbove",
    "occupiesHalfTile",
    "occupiesThreeQuarters",
    "supportsHavePrimaryColour",
    "flag27",
]
QUADRANT = ((8, 8), (8, 24), (24, 24), (24, 8))


def _bits(value, names):
    return [name for i, name in enumerate(names) if name and value >> i & 1]


def find_install(root):
    """Return (g1.dat, ObjData dir). root is the directory that contains Data/g1.dat."""
    g1 = os.path.join(root, "Data", "g1.dat")
    obj = os.path.join(root, "ObjData")
    if not os.path.isfile(g1):
        raise SystemExit(f"no Data/g1.dat under {root} (pass the RCT2 install directory)")
    if not os.path.isdir(obj):
        raise SystemExit(f"no ObjData directory under {root}")
    return g1, obj


def find_dat(obj_dir, dat_name):
    want = dat_name.upper()
    if not want.endswith(".DAT"):
        want += ".DAT"
    for name in os.listdir(obj_dir):
        if name.upper() == want:
            return os.path.join(obj_dir, name)
    raise SystemExit(f"{want} not found in {obj_dir}")


def small_scenery_placement(chunk):
    """Anchor and height the small-scenery painter uses. See Paint.SmallScenery.cpp."""
    import struct

    flags = struct.unpack_from("<I", chunk, 6)[0]
    height = chunk[10]
    names = _bits(flags, SMALL_FLAGS)
    full = "occupiesFullTile" in names
    if full and "occupiesHalfTile" in names:
        shape = "half_tile"
    elif full and "occupiesThreeQuarters" in names:
        shape = "three_quarter_tile"
    elif full:
        shape = "full_tile"
    else:
        shape = "quarter_tile"
    meta = {
        "height": height,
        "clearance_world_z": -(-height // 8) * 8,
        "flags": names,
        "shape": shape,
        "is_tree_by_height": height > 64,
    }
    if full and "occupiesHalfTile" in names:
        meta["anchor_world_xy"] = [3, 3]
    elif full and "vOffsetCentre" in names:
        meta["anchor_world_xy"] = [1, 1] if "prohibitWalls" in names else [3, 3]
    elif full:
        meta["anchor_world_xy"] = [15, 15]
    else:
        meta["anchor_world_xy_by_quadrant"] = [[x - 1, y - 1] for x, y in QUADRANT]
    return meta


def extract(rct2_data_path, dat_name, object_id, out_dir):
    g1, obj_dir = find_install(rct2_data_path)
    dat_path = find_dat(obj_dir, dat_name)
    palette = dat.load_palette(g1)
    obj_type, name, _source, strings, entries, image_bytes = dat.parse_object(dat_path)
    if entries is None:
        raise SystemExit(f"{dat_path} has no image table")
    os.makedirs(out_dir, exist_ok=True)
    chunk = dat.read_chunk(open(dat_path, "rb").read(), 16)
    placement = small_scenery_placement(chunk) if obj_type == dat.SMALL_SCENERY else {}
    sprites = []
    for index, entry in enumerate(entries):
        decoded = dat.decode_image(entry, image_bytes, palette, want_indexed=True)
        if decoded is None:
            continue
        colour, indexed = decoded
        colour.save(os.path.join(out_dir, f"{index}.png"))
        indexed.save(os.path.join(out_dir, f"{index}.indexed.png"))
        _off, width, height, x_off, y_off, flags, _zoom = entry
        sprites.append(
            {
                "index": index,
                "width": int(width),
                "height": int(height),
                "x_offset": int(x_off),
                "y_offset": int(y_off),
                "flags": int(flags),
            }
        )
    payload = {
        "object_id": object_id,
        "dat_name": name.rstrip(),
        "display_name": (strings or {}).get(0) or (strings or {}).get(1) or "",
        "object": placement,
        "sprites": sprites,
        "note": "Local extract. Do not commit this folder: it contains RCT sprites.",
    }
    with open(os.path.join(out_dir, "offsets.json"), "w", encoding="utf-8") as fh:
        json.dump(payload, fh, indent=1)
        fh.write("\n")
    np.save(os.path.join(out_dir, "palette.npy"), palette)
    return payload


def main(argv):
    parser = argparse.ArgumentParser(description="Extract one RCT2 object's sprites into a local cache.")
    parser.add_argument("--rct2-data-path", required=True, help="RCT2 install directory (contains Data/g1.dat)")
    parser.add_argument("--dat", required=True, help="DAT name, e.g. TCRP")
    parser.add_argument("--object-id", required=True, help="e.g. rct2.scenery_small.tcrp")
    parser.add_argument("--out", required=True, help="cache directory (gitignored)")
    args = parser.parse_args(argv)
    payload = extract(args.rct2_data_path, args.dat, args.object_id, args.out)
    print(f"wrote {len(payload['sprites'])} sprites to {args.out}")


if __name__ == "__main__":
    main(sys.argv[1:])
