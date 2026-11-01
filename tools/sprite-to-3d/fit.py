# Copyright (c) 2026 OpenRCT2 developers
# Licensed under the GNU General Public License version 3.

"""Silhouette fit for a conifer, using the same squashed RCT cameras as the validator.

  blender -b -P tools/sprite-to-3d/fit.py -- \\
      --params tools/sprite-to-3d/examples/rct2.scenery_small.tcrp/params.json \\
      --atlas-json path/to/object.atlas.json \\
      --atlas path/to/object.atlas.png \\
      --masks /tmp/masks.npy \\
      --iters 200 --seed 1 --out /tmp/fit.json

Build masks.npy with the venv (Blender does not need Pillow for this):

  python3 -c "import numpy,json,os; from PIL import Image; ..."

Or pass --sprites and this script will read PNGs through Blender. The search
moves one clump at a time and keeps a step when mean IoU does not fall.
It writes a JSON file. It does not overwrite the example params.
"""

from __future__ import annotations

import json
import os
import random
import sys

import bpy
import numpy as np
from mathutils import Vector

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

from archetypes.conifer import build
from conventions import ORIGIN_PIXEL, RENDER_SIZE, SIM_PER_UNIT, Z_SQUASH
from projection import PIXELS_PER_UNIT


def _args(argv):
    if "--" in argv:
        argv = argv[argv.index("--") + 1 :]
    out = {}
    key = None
    for token in argv:
        if token.startswith("--"):
            key = token[2:]
            out.setdefault(key, None)
        elif key:
            out[key] = token
            key = None
    return out


def _masks_from_sprites(path, shift, nudge):
    """Read 0.png..3.png through Blender so this file does not need Pillow."""
    width, height = RENDER_SIZE
    masks = []
    offsets = json.load(open(os.path.join(path, "offsets.json"), encoding="utf-8"))
    by_index = {int(s["index"]): s for s in offsets["sprites"]}
    for index in range(4):
        entry = by_index[index]
        image = bpy.data.images.load(os.path.join(path, f"{index}.png"))
        buf = np.empty(image.size[0] * image.size[1] * 4, np.float32)
        image.pixels.foreach_get(buf)
        rgba = (buf.reshape(image.size[1], image.size[0], 4)[::-1] * 255).astype(np.uint8)
        bpy.data.images.remove(image)
        canvas = np.zeros((height, width), np.bool_)
        ih, iw = rgba.shape[:2]
        x0 = int(round(entry["x_offset"] + ORIGIN_PIXEL[0] - shift[0]))
        y0 = int(round(entry["y_offset"] + ORIGIN_PIXEL[1] + nudge - shift[1]))
        src_x0, src_y0 = max(0, -x0), max(0, -y0)
        dst_x0, dst_y0 = max(0, x0), max(0, y0)
        dst_x1, dst_y1 = min(width, x0 + iw), min(height, y0 + ih)
        if dst_x0 < dst_x1 and dst_y0 < dst_y1:
            src = rgba[src_y0 : src_y0 + (dst_y1 - dst_y0), src_x0 : src_x0 + (dst_x1 - dst_x0), 3] > 0
            canvas[dst_y0:dst_y1, dst_x0:dst_x1] = src
        masks.append(canvas)
    return np.stack(masks)


def main():
    opt = _args(sys.argv)
    params = json.load(open(opt["params"], encoding="utf-8"))
    atlas = json.load(open(opt["atlas-json"], encoding="utf-8"))
    tiles = atlas["tiles"]
    width, height = atlas["size"]
    iters = int(opt.get("iters") or 100)
    seed = int(opt.get("seed") or 1)
    if opt.get("masks"):
        masks = np.load(opt["masks"])
    elif opt.get("sprites"):
        shift = [float(v) for v in (opt.get("shift") or "-2,0").split(",")]
        nudge = float(opt.get("y-nudge") or -1)
        masks = _masks_from_sprites(opt["sprites"], shift, nudge)
    else:
        raise SystemExit("pass --masks or --sprites")

    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    me = bpy.data.meshes.new("fit")
    ob = bpy.data.objects.new("fit", me)
    scene.collection.objects.link(ob)
    ob.scale = (1, 1, Z_SQUASH)
    image = bpy.data.images.load(opt["atlas"])
    mat = bpy.data.materials.new("m")
    mat.use_nodes = True
    nt = mat.node_tree
    bsdf = nt.nodes["Principled BSDF"]
    tex = nt.nodes.new("ShaderNodeTexImage")
    tex.image = image
    tex.interpolation = "Closest"
    nt.links.new(tex.outputs["Color"], bsdf.inputs["Base Color"])
    nt.links.new(tex.outputs["Alpha"], bsdf.inputs["Alpha"])
    mat.blend_method = "CLIP"
    mat.alpha_threshold = 0.5
    scene.render.engine = "BLENDER_WORKBENCH"
    shading = scene.display.shading
    shading.light = "FLAT"
    shading.color_type = "TEXTURE"
    scene.display.render_aa = "OFF"
    scene.render.film_transparent = True
    scene.render.resolution_x = 128
    scene.render.resolution_y = 192
    scene.render.image_settings.color_mode = "RGBA"
    cams = []
    for view, d in enumerate(((1, 1), (-1, 1), (-1, -1), (1, -1))):
        cam = bpy.data.cameras.new(f"c{view}")
        cam.type = "ORTHO"
        cam.ortho_scale = 192 / PIXELS_PER_UNIT
        cam.shift_y = 64 / 192
        co = bpy.data.objects.new(f"c{view}", cam)
        scene.collection.objects.link(co)
        direction = Vector((d[0], d[1], Z_SQUASH)).normalized()
        co.location = direction * 20
        co.rotation_euler = (-direction).to_track_quat("-Z", "Y").to_euler()
        cams.append(co)
    tmp = f"/tmp/sprite3d_fit_{seed}.png"
    scale = 1.0 / SIM_PER_UNIT

    def set_mesh(current):
        built = build(current, tiles)
        me.clear_geometry()
        me.from_pydata([(x * scale, y * scale, z * scale) for x, y, z in built.V], [], [list(f) for f in built.F])
        layer = me.uv_layers.new(name="UVMap")
        for poly in me.polygons:
            for li in poly.loop_indices:
                u, v = built.UV[me.loops[li].vertex_index]
                layer.data[li].uv = (u / width, 1.0 - v / height)
        if not me.materials:
            me.materials.append(mat)
        me.update()

    def loss(current):
        set_mesh(current)
        scores = []
        for view in range(4):
            scene.camera = cams[view]
            scene.render.filepath = tmp
            bpy.ops.render.render(write_still=True)
            loaded = bpy.data.images.load(tmp)
            buf = np.empty(128 * 192 * 4, np.float32)
            loaded.pixels.foreach_get(buf)
            bpy.data.images.remove(loaded)
            alpha = buf.reshape(192, 128, 4)[::-1, :, 3] > 0.5
            both = int(np.count_nonzero(alpha & masks[view]))
            union = int(np.count_nonzero(alpha | masks[view]))
            scores.append(both / max(union, 1))
        return 1.0 - float(np.mean(scores)), scores

    rng = random.Random(seed)
    best, best_scores = loss(params)
    print("start", best, best_scores, flush=True)
    sigma = {"x": 1.5, "y": 1.5, "z": 1.5, "a": 1.2, "b": 1.2, "th": 0.25}

    def acceptable(clump):
        return (
            5 <= clump["a"] <= 22
            and 5 <= clump["b"] <= 22
            and max(clump["a"], clump["b"]) / min(clump["a"], clump["b"]) <= 1.8
            and 10 <= clump["z"] <= 132
        )

    for step in range(iters):
        trial = json.loads(json.dumps(params))
        clump = trial["clumps"][rng.randrange(len(trial["clumps"]))]
        for key in rng.sample(list(sigma), rng.choice((1, 2, 3))):
            clump[key] += rng.gauss(0, sigma[key])
        if not acceptable(clump):
            continue
        score, scores = loss(trial)
        if score <= best:
            best, best_scores, params = score, scores, trial
        if step % 25 == 0:
            print(step, round(best, 4), [round(s, 3) for s in best_scores], flush=True)
    payload = {"loss": best, "ious": best_scores, "clumps": params["clumps"]}
    with open(opt["out"], "w", encoding="utf-8") as fh:
        json.dump(payload, fh, indent=1)
        fh.write("\n")
    print("done", best, best_scores)


if __name__ == "__main__":
    main()
