# Copyright (c) 2026 OpenRCT2 developers
# Licensed under the GNU General Public License version 3.

"""Rebuild an example object from the user's own RCT2 data.

  python3 tools/sprite-to-3d/rebuild.py \\
      --object rct2.scenery_small.tcrp \\
      --rct2-data-path /path/to/rct2 \\
      --out data/object-models/rct2.scenery_small.tcrp

The atlas is coloured from Data/g1.dat. Sprites are extracted into
tools/sprite-to-3d/cache (gitignored) and used only for the IoU check.
Pass --no-blender to export with the Python glTF writer. The mesh code
is the same either way. Blender is required for the beauty renders and
for fit.py.
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys

import numpy as np

import conventions as C
import extract as extract_mod
import rct2dat
from archetypes import build_mesh
from archetypes.conifer import triangle_count
from archetypes.conifer_atlas import write_atlas
from glb_io import write_glb
from patch_glb import patch

TOOL_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(os.path.dirname(TOOL_DIR))


def _read(path):
    with open(path, encoding="utf-8") as fh:
        return json.load(fh)


def resolve_palette(args, cache_dir):
    if args.rct2_data_path:
        g1, _obj = extract_mod.find_install(args.rct2_data_path)
        return rct2dat.load_palette(g1), "Data/g1.dat"
    if args.palette:
        return rct2dat.load_palette_file(args.palette, args.palette_base), os.path.basename(args.palette)
    indexed = os.path.join(cache_dir, "0.indexed.png") if cache_dir else ""
    if indexed and os.path.isfile(indexed):
        return rct2dat.load_palette_file(indexed, 0), "indexed sprite"
    raise SystemExit(
        "need a palette: pass --rct2-data-path (Data/g1.dat) or --palette. "
        "The repository does not contain the game palette."
    )


def blender_bin():
    return shutil.which("blender")


def export_with_blender(object_id, params_path, atlas_png, atlas_json, out_dir):
    blender = blender_bin()
    if not blender:
        raise SystemExit("blender is not on PATH. Install Blender 4.2 LTS, or pass --no-blender.")
    cmd = [
        blender,
        "-b",
        "-P",
        os.path.join(TOOL_DIR, "build.py"),
        "--",
        "--object-id",
        object_id,
        "--params",
        params_path,
        "--atlas",
        atlas_png,
        "--atlas-json",
        atlas_json,
        "--out",
        out_dir,
    ]
    print("running", " ".join(cmd))
    subprocess.check_call(cmd)


def write_meta(path, spec, params, tiles, mesh, palette_source, sprite_meta, glb_name):
    pts = np.array(mesh.V, np.float64) / C.SIM_PER_UNIT
    bmin = [float(v) for v in pts.min(axis=0)]
    bmax = [float(v) for v in pts.max(axis=0)]
    size = [bmax[i] - bmin[i] for i in range(3)]
    offsets = None
    if sprite_meta:
        offsets = [
            {
                "index": s["index"],
                "width": s["width"],
                "height": s["height"],
                "x_offset": s["x_offset"],
                "y_offset": s["y_offset"],
            }
            for s in sprite_meta["sprites"]
            if s["index"] in spec["direction_images"]
        ]
    # Keep the approved IoU on the reference object. A rebuild with sprites
    # overwrites iou.views only when the validator is asked to; the stored
    # figures stay the approved measurement until a reviewer replaces them.
    stored_iou = spec.get("approved_iou")
    meta = {
        "schema": C.SCHEMA,
        "tool_version": C.TOOL_VERSION,
        "object_id": spec["object_id"],
        "dat_name": spec["dat_name"],
        "display_name": spec["display_name"],
        "archetype": spec["archetype"],
        "size_class": spec["size_class"],
        "source_sprites": {
            "image_indices": spec["direction_images"],
            "offsets": offsets,
        },
        "alignment": spec.get("alignment") or {"sprite_shift": [0, 0], "sprite_y_nudge": -1},
        "units": {
            "blender_unit_per_tile": 1,
            "sim_units_per_tile": 32,
            "origin": "tile centre, z=0 at the element base",
            "authored_up": "Z",
            "exported_up": "Y",
            "z_squash_in_file": False,
        },
        "bounds_blender": {"min": bmin, "max": bmax, "size": size},
        "height_sim": spec["height_sim"],
        "footprint_tiles": spec["footprint_tiles"],
        "max_overhang_tiles": spec["max_overhang_tiles"],
        "base_sink_sim": spec["base_sink_sim"],
        "triangles": triangle_count(mesh),
        "texture": {
            "file": spec["object_id"] + ".atlas.png",
            "width": 256,
            "height": 256,
            "alpha_mode": "MASK",
            "alpha_cutoff": 0.5,
            "filter": "NEAREST",
            "mipmaps": False,
            "double_sided": True,
            "tiles": tiles,
            "palette_source": palette_source,
        },
        "iou": {
            "views": stored_iou["views"] if stored_iou else None,
            "mean": stored_iou["mean"] if stored_iou else None,
            "threshold_per_view": C.IOU_MIN_PER_VIEW,
            "threshold_mean": C.IOU_MIN_MEAN,
            "source": stored_iou.get("source") if stored_iou else None,
        },
        "params": {
            "trunk_rings": params.get("trunk_rings"),
            "trunk_v_denom": params.get("trunk_v_denom", 138),
            "foliage_seed": params.get("foliage_seed", 500),
            "quality": params.get("quality"),
            "clumps": params["clumps"],
            "branches": params.get("branches", []),
            "atlas": params.get("atlas"),
        },
        "review": spec.get("review") or {"status": "pending", "note": ""},
        "files": {"glb": glb_name},
    }
    with open(path, "w", encoding="utf-8") as fh:
        json.dump(meta, fh, indent=1)
        fh.write("\n")
    return meta


def main(argv):
    parser = argparse.ArgumentParser(description="Rebuild a sprite-to-3D example object.")
    parser.add_argument("--object", default="rct2.scenery_small.tcrp")
    parser.add_argument("--rct2-data-path", help="RCT2 install directory, containing Data/g1.dat")
    parser.add_argument("--sprites", help="already-extracted sprite folder; skips the DAT extract")
    parser.add_argument("--palette", help="palette file, used only when Data/g1.dat is not available")
    parser.add_argument("--palette-base", type=int, default=0, help="palette index of the first colour in --palette")
    parser.add_argument("--out", help="output directory for the GLB, atlas, and meta")
    parser.add_argument("--no-blender", action="store_true", help="export the GLB with the Python writer")
    parser.add_argument("--skip-validate", action="store_true")
    args = parser.parse_args(argv)

    example = os.path.join(TOOL_DIR, "examples", args.object)
    spec = _read(os.path.join(example, "object.json"))
    params = _read(os.path.join(example, "params.json"))
    if spec["archetype"] != "conifer":
        raise SystemExit(f"rebuild.py currently builds the conifer archetype, not {spec['archetype']}")
    out_dir = args.out or os.path.join(REPO_ROOT, "data", "object-models", spec["object_id"])
    os.makedirs(out_dir, exist_ok=True)
    cache_dir = os.path.join(TOOL_DIR, "cache", spec["object_id"])

    sprite_meta = None
    if args.sprites:
        with open(os.path.join(args.sprites, "offsets.json"), encoding="utf-8") as fh:
            sprite_meta = json.load(fh)
    elif args.rct2_data_path:
        print(f"extracting {spec['dat_name']} into {cache_dir}")
        sprite_meta = extract_mod.extract(args.rct2_data_path, spec["dat_name"], spec["object_id"], cache_dir)
        args.sprites = cache_dir

    palette, palette_source = resolve_palette(args, args.sprites)
    atlas_png = os.path.join(out_dir, spec["object_id"] + ".atlas.png")
    atlas_json = os.path.join(out_dir, spec["object_id"] + ".atlas.json")
    atlas = write_atlas(params, palette, atlas_png, atlas_json)
    mesh = build_mesh(spec["archetype"], params, atlas["tiles"])
    count = triangle_count(mesh)
    expected = spec.get("expected_triangles")
    if expected is not None and count != expected:
        raise SystemExit(f"mesh has {count} triangles, the example expects {expected}")
    print(f"mesh triangles {count}")

    glb_path = os.path.join(out_dir, spec["object_id"] + ".glb")
    params_path = os.path.join(example, "params.json")
    if args.no_blender or not blender_bin():
        if not args.no_blender:
            print("blender is not on PATH; exporting with the Python glTF writer")
        png = open(atlas_png, "rb").read()
        stats = write_glb(glb_path, mesh, png, spec["object_id"], tuple(atlas["size"]))
        print(f"wrote {glb_path} ({stats['triangles']} triangles)")
    else:
        export_with_blender(spec["object_id"], params_path, atlas_png, atlas_json, out_dir)
    patch(glb_path)
    print(f"patched samplers on {glb_path}")

    meta_path = os.path.join(out_dir, spec["object_id"] + ".meta.json")
    write_meta(meta_path, spec, params, atlas["tiles"], mesh, palette_source, sprite_meta, os.path.basename(glb_path))
    print(f"wrote {meta_path}")

    if args.skip_validate:
        return 0
    cmd = [
        sys.executable,
        os.path.join(TOOL_DIR, "validate.py"),
        "--glb",
        glb_path,
        "--meta",
        meta_path,
        "--out",
        os.path.join(TOOL_DIR, "out", spec["object_id"] + ".review.png"),
    ]
    if args.sprites:
        cmd.extend(["--sprites", args.sprites, "--require-iou"])
    print("running validator")
    return subprocess.call(cmd)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
