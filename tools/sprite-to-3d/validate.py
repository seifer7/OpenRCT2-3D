# Copyright (c) 2026 OpenRCT2 developers
# Licensed under the GNU General Public License version 3.

"""Check a GLB against the sprite-to-3D authoring conventions.

  python3 validate.py --self-test
  python3 validate.py data/object-models/rct2.scenery_small.tcrp --sprites /path/to/extracted/TCRP --out tools/sprite-to-3d/out

Exit status is 0 when every check passes, 1 when a check fails, 2 on usage errors.
Sprites are read from a local path at runtime and are not stored in the repository.
The review PNG is written only when --out is set; name it *.review.png so git ignores it.
"""

from __future__ import annotations

import argparse
import json
import os
import sys

import numpy as np

import conventions as C
from glb_io import gltf_to_blender, load_mesh
from projection import pixels_match_rct
from render_views import load_sprite_folder, place_sprite, rasterize, silhouette_iou, write_review

TOOL_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(os.path.dirname(TOOL_DIR))
PINE_DIR = os.path.join(REPO_ROOT, "data", "object-models", "rct2.scenery_small.tcrp")
PINE_ID = "rct2.scenery_small.tcrp"


class Report:
    def __init__(self):
        self.errors = []
        self.notes = []

    def error(self, message):
        self.errors.append(message)

    def note(self, message):
        self.notes.append(message)

    def ok(self):
        return not self.errors


def _close(a, b, tol):
    return abs(float(a) - float(b)) <= tol


def _load_json(path):
    with open(path, encoding="utf-8") as fh:
        return json.load(fh)


def _node_is_identity(node, report, name):
    if "matrix" in node:
        m = np.array(node["matrix"], np.float64).reshape(4, 4, order="F")
        if not np.allclose(m, np.eye(4), atol=1e-5):
            report.error(f"node {name} has a non-identity matrix; bake Y-up into the positions")
        return
    translation = node.get("translation", [0, 0, 0])
    rotation = node.get("rotation", [0, 0, 0, 1])
    scale = node.get("scale", [1, 1, 1])
    if not np.allclose(translation, [0, 0, 0], atol=1e-5):
        report.error(f"node {name} is translated by {translation}; the origin must be the tile centre at z=0")
    if not np.allclose(rotation, [0, 0, 0, 1], atol=1e-5):
        report.error(f"node {name} has a rotation; export glTF Y-up in the vertex positions")
    if not np.allclose(scale, [1, 1, 1], atol=1e-4):
        report.error(f"node {name} scale is {scale}; a Z scale near {C.Z_SQUASH:.4f} means the render squash was exported")
        if any(abs(s - C.Z_SQUASH) < 0.02 for s in scale):
            report.error(f"node {name} still has the render-only Z squash ({C.Z_SQUASH:.4f}) baked into its scale")


def _blender_bounds(positions):
    blender = np.array([gltf_to_blender(p) for p in positions], np.float64)
    return blender.min(axis=0), blender.max(axis=0), blender


def _texture_of(gltf, material):
    pbr = material.get("pbrMetallicRoughness") or {}
    tex = pbr.get("baseColorTexture")
    if not tex:
        return None
    return gltf["textures"][tex["index"]]


def check_materials(gltf, report):
    materials = gltf.get("materials") or []
    if not materials:
        report.error("GLB has no materials")
        return
    samplers = gltf.get("samplers") or []
    saw_mask = False
    for index, material in enumerate(materials):
        name = material.get("name", f"#{index}")
        mode = material.get("alphaMode", "OPAQUE")
        if mode != "MASK":
            report.error(f"material {name} alphaMode is {mode}; foliage and cards use MASK")
            continue
        saw_mask = True
        cutoff = material.get("alphaCutoff")
        if cutoff is None or abs(float(cutoff) - 0.5) > 1e-6:
            report.error(f"material {name} alphaCutoff is {cutoff}; expected 0.5")
        if material.get("doubleSided") is not True:
            report.error(f"material {name} is not doubleSided")
        pbr = material.get("pbrMetallicRoughness") or {}
        if abs(float(pbr.get("roughnessFactor", -1)) - 1.0) > 1e-6:
            report.error(f"material {name} roughnessFactor is {pbr.get('roughnessFactor')}; expected 1")
        if abs(float(pbr.get("metallicFactor", -1)) - 0.0) > 1e-6:
            report.error(f"material {name} metallicFactor is {pbr.get('metallicFactor')}; expected 0")
        spec = (material.get("extensions") or {}).get("KHR_materials_specular")
        if not spec or abs(float(spec.get("specularFactor", 1)) - 0.0) > 1e-6:
            report.error(f"material {name} needs KHR_materials_specular specularFactor 0")
        texture = _texture_of(gltf, material)
        if texture is None:
            report.error(f"material {name} has no baseColour texture")
            continue
        sampler_index = texture.get("sampler")
        if sampler_index is None or sampler_index >= len(samplers):
            report.error(f"material {name} has no sampler")
            continue
        sampler = samplers[sampler_index]
        mag = sampler.get("magFilter")
        mini = sampler.get("minFilter")
        if mag != C.NEAREST or mini != C.NEAREST:
            report.error(f"material {name} sampler mag/min is {mag}/{mini}; expected nearest ({C.NEAREST}) with no mipmaps")
        if mini in C.MIPMAP_MIN_FILTERS:
            report.error(f"material {name} minFilter {mini} enables mipmaps")
        if sampler.get("wrapS") != C.CLAMP_TO_EDGE or sampler.get("wrapT") != C.CLAMP_TO_EDGE:
            report.error(f"material {name} wrap is not clamp-to-edge")
    if not saw_mask:
        report.error("no MASK material on the mesh")


def check_meta_schema(meta, report):
    for key in C.META_REQUIRED:
        if key not in meta:
            report.error(f"meta.json is missing {key}")
    if meta.get("schema") != C.SCHEMA:
        report.error(f"meta schema is {meta.get('schema')!r}; expected {C.SCHEMA}")
    units = meta.get("units") or {}
    if units.get("exported_up") != "Y" or units.get("authored_up") != "Z":
        report.error(f"meta units axes are {units.get('authored_up')}->{units.get('exported_up')}; expected Z authored, Y exported")
    if units.get("z_squash_in_file") is not False:
        report.error("meta units.z_squash_in_file must be false")
    if units.get("sim_units_per_tile") != 32:
        report.error("meta units.sim_units_per_tile must be 32")
    size_name = meta.get("size_class")
    if size_name not in C.SIZE_CLASSES:
        report.error(f"meta size_class {size_name!r} is not a known class")
    sprites = meta.get("source_sprites") or {}
    if not sprites.get("image_indices"):
        report.error("meta source_sprites.image_indices is empty")
    review = meta.get("review") or {}
    if review.get("status") not in ("approved", "pending"):
        report.error("meta review.status must be 'approved' or 'pending'")


def check_bounds(meta, positions, report):
    bmin, bmax, _blender = _blender_bounds(positions)
    size = bmax - bmin
    report.note(
        "bounds blender min ["
        + ", ".join(f"{v:.4f}" for v in bmin)
        + "] max ["
        + ", ".join(f"{v:.4f}" for v in bmax)
        + f"] size [{size[0]:.4f}, {size[1]:.4f}, {size[2]:.4f}]"
    )
    declared = meta.get("bounds_blender") or {}
    for label, got, key in (("min", bmin, "min"), ("max", bmax, "max")):
        want = declared.get(key)
        if not want or len(want) != 3:
            report.error(f"meta bounds_blender.{key} is missing")
            continue
        if not np.allclose(got, want, atol=1e-3):
            report.error(f"GLB {label} {got.tolist()} does not match meta {want}")
    height = float(meta.get("height_sim") or 0) / C.SIM_PER_UNIT
    if height <= 0:
        report.error("meta height_sim must be positive")
    elif abs(float(bmax[2]) - height) > 0.2:
        report.error(
            f"model top is {bmax[2]:.3f} units but height_sim/32 is {height:.3f} "
            "(scale does not match the sprite height)"
        )
    sink = float(meta.get("base_sink_sim") or 0) / C.SIM_PER_UNIT
    if float(bmin[2]) < -sink - 0.02:
        report.error(f"model sinks to {bmin[2]:.3f}, past the declared base sink of {sink:.3f}")
    if float(bmin[2]) > 0.05:
        report.error(f"model base is at {bmin[2]:.3f}; it does not meet z=0")
    footprint = meta.get("footprint_tiles") or [1, 1]
    overhang = float(meta.get("max_overhang_tiles") or 0)
    limit_x = footprint[0] / 2 + overhang
    limit_y = footprint[1] / 2 + overhang
    if max(abs(float(bmin[0])), abs(float(bmax[0]))) > limit_x + 1e-3:
        report.error(f"X extent {bmin[0]:.3f}..{bmax[0]:.3f} is outside footprint {footprint[0]} plus overhang {overhang}")
    if max(abs(float(bmin[1])), abs(float(bmax[1]))) > limit_y + 1e-3:
        report.error(f"Y extent {bmin[1]:.3f}..{bmax[1]:.3f} is outside footprint {footprint[1]} plus overhang {overhang}")
    if not (bmin[0] < 0 < bmax[0] and bmin[1] < 0 < bmax[1]):
        report.error("horizontal bounds do not contain the origin; the pivot is not the tile centre")
    # Height lives on Blender Z, which is glTF Y. A Z-up file would put the height on glTF Z
    # (Blender -Y after the inverse conversion) and fail the height check above.
    if size[2] < max(size[0], size[1]) * 0.5 and float(meta.get("height_sim") or 0) > 48:
        report.error("a tall object is wider than it is tall after the Y-up conversion; the file may still be Z-up")


def check_texture(image, meta, report):
    if image is None:
        report.error("GLB has no embedded image")
        return
    height, width = image.shape[:2]
    budget = C.size_class(meta["size_class"])
    if width > budget["max_texture"] or height > budget["max_texture"]:
        report.error(f"texture {width}x{height} exceeds the {meta['size_class']} cap of {budget['max_texture']}")
    if width & (width - 1) or height & (height - 1):
        report.error(f"texture {width}x{height} is not a power of two")
    declared = meta.get("texture") or {}
    if declared.get("width") != width or declared.get("height") != height:
        report.error(f"meta texture size {declared.get('width')}x{declared.get('height')} does not match the image {width}x{height}")
    alpha = image[..., 3]
    weird = int(np.count_nonzero((alpha != 0) & (alpha != 255)))
    if weird:
        report.error(f"texture alpha is not a cut-out: {weird} texels are neither 0 nor 255")
    if declared.get("filter") != "NEAREST" or declared.get("mipmaps") is not False:
        report.error("meta texture.filter must be NEAREST and texture.mipmaps false")
    if declared.get("alpha_mode") != "MASK" or abs(float(declared.get("alpha_cutoff", -1)) - 0.5) > 1e-6:
        report.error("meta texture alpha_mode/alpha_cutoff must be MASK / 0.5")


def check_triangles(count, meta, report):
    budget = C.size_class(meta["size_class"])
    report.note(f"triangles {count} (soft {budget['soft_tris']}, hard {budget['hard_tris']})")
    if count != int(meta.get("triangles") or -1):
        report.error(f"GLB has {count} triangles but meta says {meta.get('triangles')}")
    if count > budget["hard_tris"]:
        report.error(f"{count} triangles exceeds the {meta['size_class']} hard cap of {budget['hard_tris']}")
    elif count > budget["soft_tris"]:
        report.note(f"over the soft target of {budget['soft_tris']} (still under the hard cap)")


def render_and_score(mesh, meta, sprites_dir, out_path, report, require_iou):
    renders = []
    for view in range(4):
        renders.append(rasterize(mesh["positions"], mesh["uvs"], mesh["indices"], mesh["image"], view))
    coverage = [int(np.count_nonzero(im[..., 3] > 0)) for im in renders]
    report.note("render coverage " + ", ".join(str(c) for c in coverage))
    for view, count in enumerate(coverage):
        if count < 500:
            report.error(f"view {view} rendered {count} opaque pixels; the projection or the alpha cut is empty")
    placed = None
    ious = None
    if sprites_dir:
        shift = meta.get("alignment", {}).get("sprite_shift", [0, 0])
        nudge = meta.get("alignment", {}).get("sprite_y_nudge", -1)
        loaded = load_sprite_folder(sprites_dir)
        placed = [place_sprite(s, shift, nudge) for s in loaded]
        ious = []
        for view in range(4):
            score, sprite_px, render_px = silhouette_iou(placed[view], renders[view])
            ious.append(score)
            report.note(f"view {view} IoU {score:.3f} (sprite {sprite_px}px, render {render_px}px)")
            span_s = _span(placed[view][..., 3] > 0)
            span_r = _span(renders[view][..., 3] > 0)
            if span_s[1] == 0 or span_r[1] == 0:
                report.error(f"view {view} has an empty silhouette")
            else:
                ratio = span_r[1] / span_s[1]
                if ratio < 0.7 or ratio > 1.35:
                    report.error(
                        f"view {view} height ratio render/sprite is {ratio:.2f}; "
                        "the model scale does not match the sprite"
                    )
        mean = float(np.mean(ious))
        report.note(f"mean IoU {mean:.3f}")
        if any(s < C.IOU_MIN_PER_VIEW for s in ious) or mean < C.IOU_MIN_MEAN:
            report.error(
                f"IoU below the definition of done (each view >= {C.IOU_MIN_PER_VIEW}, "
                f"mean >= {C.IOU_MIN_MEAN})"
            )
        declared = (meta.get("iou") or {}).get("views")
        if declared and len(declared) == 4:
            # Stored figures are the approved measurement. A fresh rasteriser will not
            # hit them exactly; this only catches a meta that claims a pass it didn't earn.
            if any(float(v) < C.IOU_MIN_PER_VIEW for v in declared):
                report.error("meta iou.views records a view under the threshold")
    elif require_iou:
        report.error("IoU was required but no --sprites directory was given")
    else:
        report.note("IoU not recomputed (no local sprites). Stored meta figures are the approved measurement.")
    if out_path:
        write_review(out_path, renders, placed)
        report.note(f"wrote {out_path}")
    return ious


def _span(mask):
    ys = np.nonzero(mask.any(axis=1))[0]
    xs = np.nonzero(mask.any(axis=0))[0]
    if len(xs) == 0 or len(ys) == 0:
        return (0, 0)
    return (int(xs[-1] - xs[0] + 1), int(ys[-1] - ys[0] + 1))


def validate_files(glb_path, meta_path, sprites_dir, out_path, require_iou):
    report = Report()
    if os.path.basename(glb_path).endswith(".review.png"):
        report.error("refusing to treat a review image as a model")
    meta = _load_json(meta_path)
    check_meta_schema(meta, report)
    object_id = meta.get("object_id")
    if os.path.basename(glb_path) != f"{object_id}.glb":
        report.error(f"GLB file name should be {object_id}.glb")
    if os.path.basename(meta_path) != f"{object_id}.meta.json":
        report.error(f"meta file name should be {object_id}.meta.json")
    extras = {}
    try:
        mesh = load_mesh(glb_path)
    except Exception as exc:
        report.error(f"could not read GLB: {exc}")
        return report
    gltf = mesh["gltf"]
    extras = (gltf.get("extras") or {}).get("openrct2_3d") or {}
    if extras.get("z_squash") is True:
        report.error("GLB extras say the render-only Z squash is in the file")
    if gltf.get("asset", {}).get("version") != "2.0":
        report.error("glTF asset version is not 2.0")
    for node in gltf.get("nodes") or []:
        _node_is_identity(node, report, node.get("name", "node"))
    check_materials(gltf, report)
    if meta.get("size_class") in C.SIZE_CLASSES:
        check_triangles(mesh["triangles"], meta, report)
        check_texture(mesh["image"], meta, report)
        check_bounds(meta, mesh["positions"], report)
    render_and_score(mesh, meta, sprites_dir, out_path, report, require_iou)
    return report


def _print_report(report):
    for line in report.notes:
        print(f"  {line}")
    for line in report.errors:
        print(f"  FAIL {line}")
    if report.ok():
        print("validator: pass")
    else:
        print(f"validator: fail ({len(report.errors)})")


def self_test():
    """Structural check of the committed pine, plus the projection identity."""
    report = Report()
    error = pixels_match_rct()
    report.note(f"squashed camera vs RCT formula, max error {error:.3e} px")
    if error > 0.05:
        report.error("the render camera does not match the RCT projection")
    glb_path = os.path.join(PINE_DIR, PINE_ID + ".glb")
    meta_path = os.path.join(PINE_DIR, PINE_ID + ".meta.json")
    if not os.path.isfile(glb_path) or not os.path.isfile(meta_path):
        report.error("committed pine GLB or meta is missing")
        _print_report(report)
        return report
    # The scripts must still build the same triangle count as the committed mesh.
    sys.path.insert(0, TOOL_DIR)
    from archetypes.conifer import build, triangle_count

    meta = _load_json(meta_path)
    tiles = (meta.get("texture") or {}).get("tiles")
    if not tiles:
        report.error("meta has no texture.tiles, so the mesh cannot be rebuilt")
    else:
        mesh = build(meta["params"], tiles)
        count = triangle_count(mesh)
        if count != int(meta["triangles"]):
            report.error(f"rebuilt mesh has {count} triangles, meta says {meta['triangles']}")
        else:
            report.note(f"rebuilt mesh matches meta triangle count ({count})")
        pts = np.array(mesh.V, np.float64) / C.SIM_PER_UNIT
        built_min, built_max = pts.min(axis=0), pts.max(axis=0)
        want_min = np.array(meta["bounds_blender"]["min"], np.float64)
        want_max = np.array(meta["bounds_blender"]["max"], np.float64)
        if not np.allclose(built_min, want_min, atol=1e-4) or not np.allclose(built_max, want_max, atol=1e-4):
            report.error(
                "rebuilt bounds "
                f"{built_min.tolist()} .. {built_max.tolist()} "
                f"do not match meta {want_min.tolist()} .. {want_max.tolist()}"
            )
        else:
            report.note("rebuilt bounds match the meta")
    out = os.path.join(TOOL_DIR, "out", PINE_ID + ".review.png")
    file_report = validate_files(glb_path, meta_path, None, out, require_iou=False)
    report.notes.extend(file_report.notes)
    report.errors.extend(file_report.errors)
    _print_report(report)
    return report


def main(argv):
    parser = argparse.ArgumentParser(description="Validate an object GLB against the sprite-to-3D conventions.")
    parser.add_argument("model_dir", nargs="?", help="directory containing <object id>.glb and <object id>.meta.json")
    parser.add_argument("--glb", help="GLB path (overrides model_dir)")
    parser.add_argument("--meta", help="meta.json path (overrides model_dir)")
    parser.add_argument("--sprites", help="extracted sprite folder (0.png..3.png and offsets.json). Not committed.")
    parser.add_argument("--out", help="review PNG path. Use a *.review.png name; it may contain sprites.")
    parser.add_argument("--require-iou", action="store_true", help="fail if sprites were not supplied")
    parser.add_argument("--self-test", action="store_true", help="check the committed Corsican Pine")
    args = parser.parse_args(argv)
    if args.self_test:
        report = self_test()
        return 0 if report.ok() else 1
    if args.model_dir and not args.glb:
        # The directory name is not the object id (that can contain dots). Read the meta.
        metas = [n for n in os.listdir(args.model_dir) if n.endswith(".meta.json")]
        if len(metas) != 1:
            print("expected exactly one *.meta.json in the model directory", file=sys.stderr)
            return 2
        args.meta = os.path.join(args.model_dir, metas[0])
        object_id = metas[0][: -len(".meta.json")]
        args.glb = os.path.join(args.model_dir, object_id + ".glb")
    if not args.glb or not args.meta:
        parser.error("pass a model directory, or --glb and --meta, or --self-test")
    if args.out is None and args.sprites:
        args.out = os.path.join(TOOL_DIR, "out", os.path.splitext(os.path.basename(args.glb))[0] + ".review.png")
    report = validate_files(args.glb, args.meta, args.sprites, args.out, args.require_iou)
    _print_report(report)
    return 0 if report.ok() else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
