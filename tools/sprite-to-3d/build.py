# Copyright (c) 2026 OpenRCT2 developers
# Licensed under the GNU General Public License version 3.

"""Blender 4.2 LTS headless build.

  blender -b -P tools/sprite-to-3d/build.py -- \\
      --object-id rct2.scenery_small.tcrp \\
      --params tools/sprite-to-3d/examples/rct2.scenery_small.tcrp/params.json \\
      --atlas data/object-models/rct2.scenery_small.tcrp/rct2.scenery_small.tcrp.atlas.png \\
      --atlas-json data/object-models/rct2.scenery_small.tcrp/rct2.scenery_small.tcrp.atlas.json \\
      --out data/object-models/rct2.scenery_small.tcrp

Scale: 1 Blender unit = 1 tile = 32 sim units on every axis.
Origin: tile centre, z = 0 at the element base. Blender +Z up.
The glTF exporter writes Y-up. Comparison renders scale Z by sqrt(2/3)
and that scale is cleared before export. patch_glb.py then forces
nearest filtering, no mipmaps, and alpha MASK.
"""

from __future__ import annotations

import json
import math
import os
import sys

import bpy
from mathutils import Vector

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import patch_glb
from archetypes.conifer import build
from conventions import SIM_PER_UNIT, Z_SQUASH
from projection import PIXELS_PER_UNIT


def _args(argv):
    if "--" in argv:
        argv = argv[argv.index("--") + 1 :]
    else:
        argv = []
    out = {}
    key = None
    flags = set()
    for token in argv:
        if token.startswith("--"):
            key = token[2:]
            flags.add(key)
            out.setdefault(key, None)
        elif key:
            out[key] = token
            key = None
    return out, flags


def main():
    opt, flags = _args(sys.argv)
    object_id = opt["object-id"]
    params = json.load(open(opt["params"], encoding="utf-8"))
    atlas_json = json.load(open(opt["atlas-json"], encoding="utf-8"))
    out_dir = opt["out"]
    os.makedirs(out_dir, exist_ok=True)
    tiles = atlas_json["tiles"]
    width, height = atlas_json["size"]

    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    mesh_data = build(params, tiles)
    me = bpy.data.meshes.new(object_id)
    ob = bpy.data.objects.new(object_id, me)
    scene.collection.objects.link(ob)
    scale = 1.0 / SIM_PER_UNIT
    me.from_pydata([(x * scale, y * scale, z * scale) for x, y, z in mesh_data.V], [], [list(f) for f in mesh_data.F])
    uv_layer = me.uv_layers.new(name="UVMap")
    for poly in me.polygons:
        for li in poly.loop_indices:
            u, v = mesh_data.UV[me.loops[li].vertex_index]
            # Blender's UV origin is the bottom-left. The glTF exporter flips V,
            # which lands on the glTF top-left origin used by glb_io.py.
            uv_layer.data[li].uv = (u / width, 1.0 - v / height)
    for poly in me.polygons:
        poly.use_smooth = False
    me.validate()
    me.update()

    image = bpy.data.images.load(opt["atlas"])
    image.pack()
    mat = bpy.data.materials.new(object_id + "_mat")
    mat.use_nodes = True
    nt = mat.node_tree
    bsdf = nt.nodes["Principled BSDF"]
    tex = nt.nodes.new("ShaderNodeTexImage")
    tex.image = image
    tex.interpolation = "Closest"
    nt.links.new(tex.outputs["Color"], bsdf.inputs["Base Color"])
    nt.links.new(tex.outputs["Alpha"], bsdf.inputs["Alpha"])
    bsdf.inputs["Roughness"].default_value = 1.0
    bsdf.inputs["Specular IOR Level"].default_value = 0.0
    for attr, value in (
        ("blend_method", "CLIP"),
        ("alpha_threshold", 0.5),
        ("use_backface_culling", False),
        ("surface_render_method", "DITHERED"),
    ):
        if hasattr(mat, attr):
            setattr(mat, attr, value)
    me.materials.append(mat)
    ntri = sum(len(p.vertices) - 2 for p in me.polygons)

    if "views" in flags or "beauty" in flags:
        _render(scene, ob, out_dir, flags)

    ob.scale = (1, 1, 1)
    for obj in scene.objects:
        obj.select_set(obj.name == object_id)
    bpy.context.view_layer.objects.active = ob
    glb_path = os.path.join(out_dir, object_id + ".glb")
    bpy.ops.export_scene.gltf(
        filepath=glb_path,
        export_format="GLB",
        use_selection=True,
        export_apply=True,
        export_yup=True,
    )
    patch_glb.patch(glb_path)
    print("TRIS", ntri, "GLB", glb_path)


def _render(scene, ob, out_dir, flags):
    """RCT-angle and beauty renders. Z squash is applied only while rendering."""
    scene.render.engine = "BLENDER_WORKBENCH"
    shading = scene.display.shading
    shading.light = "FLAT"
    shading.color_type = "TEXTURE"
    scene.display.render_aa = "OFF"
    scene.render.film_transparent = True
    scene.view_settings.view_transform = "Standard"
    scene.render.image_settings.file_format = "PNG"
    scene.render.image_settings.color_mode = "RGBA"
    if "views" in flags:
        rx, ry = 128, 192
        scene.render.resolution_x = rx
        scene.render.resolution_y = ry
        scene.render.resolution_percentage = 100
        ob.scale = (1, 1, Z_SQUASH)
        render_dir = os.path.join(out_dir, "_render")
        os.makedirs(render_dir, exist_ok=True)
        for view, d in enumerate(((1, 1), (-1, 1), (-1, -1), (1, -1))):
            cam_data = bpy.data.cameras.new(f"RCTcam{view}")
            cam_data.type = "ORTHO"
            cam_data.ortho_scale = max(rx, ry) / PIXELS_PER_UNIT
            cam_data.shift_y = 64 / max(rx, ry)
            cam = bpy.data.objects.new(f"RCTcam{view}", cam_data)
            scene.collection.objects.link(cam)
            direction = Vector((d[0], d[1], Z_SQUASH)).normalized()
            cam.location = direction * 20
            cam.rotation_euler = (-direction).to_track_quat("-Z", "Y").to_euler()
            scene.camera = cam
            scene.render.filepath = os.path.join(render_dir, f"view{view}.png")
            bpy.ops.render.render(write_still=True)
    if "beauty" not in flags:
        ob.scale = (1, 1, 1)
        return
    scene.display.render_aa = "16"
    scene.render.film_transparent = False
    scene.render.resolution_x = 900
    scene.render.resolution_y = 1200
    world = bpy.data.worlds.new("w")
    scene.world = world
    world.color = (0.55, 0.62, 0.7)
    cam_data = bpy.data.cameras.new("beauty")
    cam_data.lens = 50
    cam = bpy.data.objects.new("beauty_cam", cam_data)
    scene.collection.objects.link(cam)
    scene.camera = cam

    def orbit(target, dist, az, el):
        return Vector(target) + dist * Vector((math.cos(el) * math.cos(az), math.cos(el) * math.sin(az), math.sin(el)))

    for name, kz, target, dist in (
        ("beauty.png", 1.0, (0, 0, 2.1), 6.7),
        ("beauty_squashed_ref.png", Z_SQUASH, (0, 0, 1.75), 5.7),
    ):
        ob.scale = (1, 1, kz)
        loc = orbit(target, dist, math.radians(-55), math.radians(25))
        cam.location = loc
        cam.rotation_euler = (Vector(target) - cam.location).to_track_quat("-Z", "Y").to_euler()
        scene.render.filepath = os.path.join(out_dir, name)
        bpy.ops.render.render(write_still=True)
    ob.scale = (1, 1, 1)


if __name__ == "__main__":
    main()
