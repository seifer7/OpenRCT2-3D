# Copyright (c) 2026 OpenRCT2 developers
# Licensed under the GNU General Public License version 3.

"""Create a local editable scene and model-only preview, without changing the GLB.

blender -b -P tools/sprite-to-3d/examples/rct2.scenery_small.tsq/preview.py

The runtime GLB is authored by rebuild.py, not by this optional preview script.
Works with the locally installed Blender 5.2; no source game data is needed.
"""

from pathlib import Path
import argparse
import math
import sys

import bpy
from mathutils import Vector

ROOT = Path(__file__).resolve().parents[4]
OBJECT_ID = "rct2.scenery_small.tsq"
OUT = ROOT / "tools/sprite-to-3d/out" / OBJECT_ID


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--glb", type=Path, default=ROOT / "data/object-models" / (OBJECT_ID + ".glb"))
    parser.add_argument("--out", type=Path, default=OUT)
    options = parser.parse_args(sys.argv[sys.argv.index("--")+1:] if "--" in sys.argv else [])
    output = options.out.resolve()
    output.mkdir(parents=True, exist_ok=True)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=str(options.glb.resolve()))
    scene = bpy.context.scene
    model = next(ob for ob in scene.objects if ob.type == "MESH")
    model.name = OBJECT_ID
    for mat in model.data.materials:
        for node in mat.node_tree.nodes:
            if node.type == "TEX_IMAGE":
                node.interpolation = "Closest"
                node.image.pack()
    bpy.ops.wm.save_as_mainfile(filepath=str(output / (OBJECT_ID + ".blend")))

    scene.render.engine = "BLENDER_WORKBENCH"
    scene.display.shading.light = "FLAT"
    scene.display.shading.color_type = "TEXTURE"
    scene.display.shading.show_shadows = False
    scene.display.shading.show_cavity = False
    scene.display.shading.show_specular_highlight = False
    scene.display.shading.background_type = "WORLD"
    scene.world = bpy.data.worlds.new("Preview background")
    scene.world.color = (0.12, 0.14, 0.17)
    scene.display.render_aa = "16"
    scene.render.film_transparent = False
    scene.view_settings.view_transform = "Standard"
    scene.render.resolution_x = 768
    scene.render.resolution_y = 768
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = "PNG"
    scene.render.image_settings.color_mode = "RGBA"
    camera = bpy.data.objects.new("Preview camera", bpy.data.cameras.new("Preview camera"))
    scene.collection.objects.link(camera)
    scene.camera = camera
    camera.data.type = "ORTHO"
    camera.data.ortho_scale = 1.6
    target = Vector((0, 0, 0.56))
    for index, (x, y) in enumerate(((1, 1), (-1, 1), (-1, -1), (1, -1))):
        camera.location = target + Vector((x, y, math.sqrt(2 / 3))) * 3
        camera.rotation_euler = (target - camera.location).to_track_quat("-Z", "Y").to_euler()
        scene.render.filepath = str(output / f"view{index}.png")
        bpy.ops.render.render(write_still=True)
    camera.data.type = "PERSP"
    camera.data.lens = 55
    camera.location = target + Vector((-2.5, -2.7, 1.3))
    camera.rotation_euler = (target - camera.location).to_track_quat("-Z", "Y").to_euler()
    scene.render.filepath = str(output / "albedo.png")
    bpy.ops.render.render(write_still=True)
    scene.display.shading.light = "STUDIO"
    scene.render.filepath = str(output / "beauty.png")
    bpy.ops.render.render(write_still=True)
    print("Editable true-scale mesh and previews:", output)


if __name__ == "__main__":
    main()
