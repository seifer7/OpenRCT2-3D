# Copyright (c) 2026 OpenRCT2 developers
# Licensed under the GNU General Public License version 3.

"""Blender-only connected topiary construction; called by author.py."""

import json
import math
from pathlib import Path
import sys

import bpy
from mathutils import Vector

HERE = Path(__file__).resolve().parent
TOOLS = HERE.parents[2]
sys.path.insert(0, str(TOOLS))
import patch_glb


def main():
    out = Path(sys.argv[sys.argv.index("--") + 1]).resolve()
    params = json.loads((HERE / "params.json").read_text())
    bpy.ops.wm.read_factory_settings(use_empty=True)
    objects = []
    for part in params["parts"]:
        for sign in ((-1, 1) if part.get("pair") else (1,)):
            center = part["center"].copy()
            center[1] *= sign
            bpy.ops.mesh.primitive_uv_sphere_add(segments=24, ring_count=16, location=center)
            ob = bpy.context.object
            ob.name = part["name"]
            ob.scale = part["radii"]
            ob.rotation_euler.y = math.radians(part.get("tilt", 0))
            objects.append(ob)
    rings = params["tail"]["rings"]
    sides = params["tail"]["sides"]
    vertices, faces = [], []
    for i, ring in enumerate(rings):
        center = Vector(ring[:3])
        tangent = (Vector(rings[min(i + 1, len(rings)-1)][:3]) - Vector(rings[max(0, i-1)][:3])).normalized()
        across = Vector((0, 1, 0))
        radial = across.cross(tangent).normalized()
        for j in range(sides):
            angle = 2 * math.pi * j / sides
            vertices.append(center + radial * (ring[3]*math.cos(angle)) + across * (ring[4]*math.sin(angle)))
        if i:
            for j in range(sides):
                k = (j+1) % sides
                faces.append(((i-1)*sides+j, (i-1)*sides+k, i*sides+k, i*sides+j))
    faces.extend([tuple(reversed(range(sides))), tuple((len(rings)-1)*sides+j for j in range(sides))])
    mesh = bpy.data.meshes.new("Curled bushy tail")
    mesh.from_pydata(vertices, [], faces)
    tail = bpy.data.objects.new("Curled bushy tail", mesh)
    bpy.context.collection.objects.link(tail)
    objects.append(tail)
    bpy.ops.object.select_all(action="DESELECT")
    for ob in objects:
        ob.select_set(True)
    bpy.context.view_layer.objects.active = objects[0]
    bpy.ops.object.join()
    ob = bpy.context.object
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    remesh = ob.modifiers.new("Fuse clipped hedge volumes", "REMESH")
    remesh.mode = "VOXEL"
    remesh.voxel_size = params["voxel_size"]
    remesh.use_smooth_shade = True
    bpy.ops.object.modifier_apply(modifier=remesh.name)
    smooth = ob.modifiers.new("Round clipped foliage joins", "SMOOTH")
    smooth.factor = 0.6
    smooth.iterations = 4
    bpy.ops.object.modifier_apply(modifier=smooth.name)
    ob.data.calc_loop_triangles()
    decimate = ob.modifiers.new("Quarter tile triangle budget", "DECIMATE")
    decimate.ratio = params["triangle_target"] / len(ob.data.loop_triangles)
    decimate.use_collapse_triangulate = True
    decimate.use_symmetry = False
    bpy.ops.object.modifier_apply(modifier=decimate.name)
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.mesh.normals_make_consistent(inside=False)
    bpy.ops.object.mode_set(mode="OBJECT")
    mesh = ob.data
    mesh.validate(verbose=True)
    mesh.update()
    low = min(v.co.z for v in mesh.vertices)
    for v in mesh.vertices:
        v.co.z = max(0, v.co.z - low)
    uv = mesh.uv_layers.new(name="FoliageUV")
    # Dominant-axis projection keeps approximately one texel per sim unit.
    # Every texel is foliage: no dark skirt core or empty conifer-card borders.
    for poly in mesh.polygons:
        axis = max(range(3), key=lambda k: abs(poly.normal[k]))
        a, b = [k for k in range(3) if k != axis]
        for li in poly.loop_indices:
            p = mesh.vertices[mesh.loops[li].vertex_index].co
            uv.data[li].uv = ((p[a]+48)/128, 1-(p[b]+48)/128)
        poly.use_smooth = True
    material = bpy.data.materials.new("Clipped squirrel hedge")
    material.use_nodes = True
    bsdf = material.node_tree.nodes.get("Principled BSDF")
    texture = material.node_tree.nodes.new("ShaderNodeTexImage")
    texture.image = bpy.data.images.load(str(out / "foliage.png"))
    texture.image.pack()
    texture.interpolation = "Closest"
    material.node_tree.links.new(texture.outputs["Color"], bsdf.inputs["Base Color"])
    material.node_tree.links.new(texture.outputs["Alpha"], bsdf.inputs["Alpha"])
    material.surface_render_method = "DITHERED"
    bsdf.inputs["Roughness"].default_value = 1
    bsdf.inputs["Metallic"].default_value = 0
    bsdf.inputs["Specular IOR Level"].default_value = 0
    material.use_backface_culling = False
    mesh.materials.clear()
    mesh.materials.append(material)
    object_id = "rct2.scenery_small.tsq"
    ob.name = object_id
    payload = {
        "V": [list(v.co) for v in mesh.vertices],
        "F": [list(p.vertices) for p in mesh.polygons],
        "blender_version": bpy.app.version_string,
    }
    (out / "geometry.json").write_text(json.dumps(payload))
    for v in mesh.vertices:
        v.co /= 32
    mesh.update()
    bpy.ops.wm.save_as_mainfile(filepath=str(out / (object_id + ".blend")))
    bpy.ops.export_scene.gltf(filepath=str(out / (object_id + ".glb")), export_format="GLB", use_selection=True, export_yup=True, export_apply=True)
    patch_glb.patch(str(out / (object_id + ".glb")))


if __name__ == "__main__":
    main()
