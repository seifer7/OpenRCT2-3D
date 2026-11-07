# Copyright (c) 2026 OpenRCT2 developers
# Licensed under the GNU General Public License version 3.

"""Build the revised review candidate with Python and Blender, not the rejected GLB."""

import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys

import numpy as np
from PIL import Image, ImageFilter

HERE = Path(__file__).resolve().parent
TOOLS = HERE.parents[2]
sys.path.insert(0, str(TOOLS))
from archetypes.conifer import Mesh
from rebuild import check_histograms, measure_glb_iou, write_meta
from rct2dat import load_standard_palette


def paint(params):
    """Dense small leaf clusters, histogram matched without post-paint dark rims."""
    spec = params["atlas"]
    w, h = spec["size"]
    rng = np.random.default_rng(spec["seed"])
    noise = rng.integers(0, 256, (h, w), dtype=np.uint8)
    broad = np.asarray(Image.fromarray(noise).filter(ImageFilter.GaussianBlur(1.0)), dtype=float)
    fine = rng.normal(0, 1, (h, w))
    shade = broad + fine * 2.5
    hist = spec["foliage_hist"]
    keys = sorted(int(k) for k in hist)
    counts = np.array([hist[str(k)] for k in keys], dtype=float)
    indices = np.empty((h,w), dtype=np.uint8)
    # Balance each local UV-sized patch, not only the largely unused atlas.
    # Preserve clustered detail without letting one sampled region turn dark.
    for y in range(0,h,16):
        for x in range(0,w,16):
            block = shade[y:y+16,x:x+16]
            cuts = np.rint(np.cumsum(counts)/counts.sum()*block.size).astype(int)
            order = np.argsort(block, axis=None, kind="stable")
            result = np.empty(block.size, dtype=np.uint8)
            start = 0
            for k, end in zip(keys, cuts):
                result[order[start:end]] = k
                start = end
            indices[y:y+16,x:x+16] = result.reshape(block.shape)
    rgba = np.full((h,w,4), 255, dtype=np.uint8)
    rgba[:,:,:3] = load_standard_palette()[indices]
    return rgba


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--blender", default=shutil.which("blender"))
    parser.add_argument("--sprites")
    args = parser.parse_args()
    if not args.blender:
        parser.error("pass --blender with the local Blender executable path")
    out = TOOLS / "out/squirrel-revision2"
    out.mkdir(parents=True, exist_ok=True)
    params = json.loads((HERE / "params.json").read_text())
    check_histograms(params)
    Image.fromarray(paint(params)).save(out / "foliage.png")
    subprocess.run([args.blender, "-b", "-P", str(HERE / "sculpt.py"), "--", str(out)], check=True)
    payload = json.loads((out / "geometry.json").read_text())
    mesh = Mesh()
    mesh.V, mesh.F = payload["V"], payload["F"]
    spec = json.loads((HERE.parent / "object.json").read_text())
    spec["archetype"] = "squirrel_topiary"
    spec["review"] = {"status":"pending", "note":"Revision 2: connected hedge sculpture and corrected foliage sampling. Prior conifer-lump model visually rejected. Awaiting owner review."}
    spec["triangle_justification"] = "Connected sculpted anatomy and curved tail decimated below the quarter-tree hard budget."
    object_id = spec["object_id"]
    glb = out / (object_id + ".glb")
    measured = measure_glb_iou(str(glb), spec, args.sprites)
    sprite_meta = json.loads((Path(args.sprites)/"offsets.json").read_text()) if args.sprites else None
    write_meta(str(out/(object_id+".meta.json")), spec, params, {"foliage":[0,0,128,128]}, mesh,
               "ImageImporter.h StandardPalette; dominant greens counted from local TSQ sprites",
               sprite_meta, glb.name, "Blender " + payload["blender_version"], measured)
    print("Candidate:", glb)
    cmd = [sys.executable, str(TOOLS/"validate.py"), "--glb", str(glb), "--meta", str(out/(object_id+".meta.json")), "--out", str(out/(object_id+".review.png"))]
    if args.sprites:
        cmd += ["--sprites", args.sprites, "--require-iou"]
    return subprocess.call(cmd)


if __name__ == "__main__":
    raise SystemExit(main())
