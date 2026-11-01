# Copyright (c) 2026 OpenRCT2 developers
# Licensed under the GNU General Public License version 3.

"""Check a GLB against the sprite-to-3D authoring conventions.

  python3 validate.py --self-test
  python3 validate.py data/object-models/rct2.scenery_small.tcrp.glb --sprites /path/to/extracted/TCRP --out tools/sprite-to-3d/out

Exit status is 0 when every check passes, 1 when a check fails, 2 on usage errors.
Sprites are read from a local path at runtime and are not stored in the repository.
The review PNG is written only when --out is set; name it *.review.png so git ignores it.
"""

from __future__ import annotations

import argparse
import json
import math
import os
import subprocess
import sys
import tempfile

import numpy as np

import conventions as C
from glb_io import gltf_to_blender, load_mesh
from projection import pixels_match_rct
from render_views import load_sprite_folder, place_sprite, rasterize, silhouette_iou, write_review

TOOL_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(os.path.dirname(TOOL_DIR))
PINE_ID = C.PINE_OBJECT_ID
FLAT_GLB = os.path.join(REPO_ROOT, "data", "object-models", PINE_ID + ".glb")
FLAT_META = os.path.join(REPO_ROOT, "data", "object-models", PINE_ID + ".meta.json")
_PALETTE = None


class Report:
    def __init__(self):
        self.errors = []
        self.warnings = []
        self.notes = []
        self.rows = []

    def error(self, message):
        self.errors.append(message)

    def warn(self, message):
        self.warnings.append(message)

    def note(self, message):
        self.notes.append(message)

    def row(self, name, status, detail=""):
        self.rows.append((name, status, detail))
        if status == "FAIL":
            self.error(f"{name}: {detail}" if detail else name)
        elif status == "WARN":
            self.warn(f"{name}: {detail}" if detail else name)

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


def _palette():
    global _PALETTE
    if _PALETTE is None:
        import rct2dat

        colours = rct2dat.load_standard_palette()
        _PALETTE = {tuple(int(c) for c in row) for row in colours}
    return _PALETTE


def _object_id(meta, glb_path):
    if meta.get("object_id"):
        return meta["object_id"]
    name = os.path.basename(glb_path)
    return name[:-4] if name.endswith(".glb") else name


def _size_class_name(meta, object_id):
    if meta.get("size_class") in C.SIZE_CLASSES:
        return meta["size_class"]
    text = " ".join(
        str(meta.get(key) or "") for key in ("object", "display_name", "archetype")
    ).lower()
    if object_id == C.PINE_OBJECT_ID or "pine" in text or "conifer" in text or "tree" in text:
        return "tree_1tile"
    return "prop_tile"


def _is_canopy(meta, object_id, size_name):
    if (C.SIZE_CLASSES.get(size_name) or {}).get("canopy"):
        return True
    if object_id == C.PINE_OBJECT_ID:
        return True
    text = str(meta.get("object") or "").lower()
    return "pine" in text or "tree" in text


def _height_sim(meta):
    if meta.get("height_sim") is not None:
        return float(meta["height_sim"])
    box = (meta.get("units_and_scale") or {}).get("bbox_sim") or {}
    if box.get("max"):
        return float(box["max"][2])
    return None


def _justification(meta):
    texture = meta.get("texture") or {}
    for value in (texture.get("justification"), texture.get("atlas_justification"), meta.get("texture_justification")):
        if value and str(value).strip():
            return str(value).strip()
    return ""


def _identifier_safe(name):
    if not name or any(token in name for token in ("/", "\\", ":", "..")):
        return False
    reserved = {"CON", "PRN", "AUX", "NUL"}
    reserved.update(f"COM{i}" for i in range(1, 10))
    reserved.update(f"LPT{i}" for i in range(1, 10))
    return name.upper() not in reserved


def _texel_density(positions, uvs, indices, width, height):
    """10th percentile of edges at least 0.2 units long, in texels per unit.

    Foliage cards are denser than the trunk. The lower tail of the long edges
    is the trunk and the branches, which are authored near 1 texel per sim unit
    (32 per Blender unit). The Corsican Pine lands inside the ±25% band.
    """
    samples = []
    for offset in range(0, len(indices), 3):
        tri = indices[offset : offset + 3]
        for a, b in ((tri[0], tri[1]), (tri[1], tri[2]), (tri[2], tri[0])):
            world = float(np.linalg.norm(positions[a] - positions[b]))
            if world < 0.2:
                continue
            du = (uvs[a, 0] - uvs[b, 0]) * width
            dv = (uvs[a, 1] - uvs[b, 1]) * height
            samples.append(math.hypot(float(du), float(dv)) / world)
    if len(samples) < 8:
        return None
    return float(np.percentile(samples, 10))


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
        # glTF's default roughnessFactor is 1. The Blender exporter omits it.
        if abs(float(pbr.get("roughnessFactor", 1.0)) - 1.0) > 1e-6:
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
        mean = float(np.mean(ious))
        report.note(f"mean IoU {mean:.3f}")
        iou_ok = all(s >= C.IOU_MIN_PER_VIEW for s in ious) and mean >= C.IOU_MIN_MEAN
        report.row(
            "silhouette IoU",
            "PASS" if iou_ok else "FAIL",
            f"views {[round(s, 3) for s in ious]} mean {mean:.3f}",
        )
        # Pixel-area ratio of the render mask to the placed sprite. 1 px = 1 sim unit
        # in this projection, so the spans are also the height check against the sprite.
        ratios = []
        height_ok = True
        for view in range(4):
            sprite_px = int(np.count_nonzero(placed[view][..., 3] > 0))
            render_px = int(np.count_nonzero(renders[view][..., 3] > 0))
            ratios.append(render_px / sprite_px if sprite_px else 0)
            span_s = _span(placed[view][..., 3] > 0)[1]
            span_r = _span(renders[view][..., 3] > 0)[1]
            if abs(span_r - span_s) > C.HEIGHT_TOLERANCE_SIM:
                height_ok = False
        ratio_ok = all(C.AREA_RATIO[0] <= r <= C.AREA_RATIO[1] for r in ratios)
        report.row(
            "area ratio",
            "PASS" if ratio_ok else "FAIL",
            ", ".join(f"{r:.2f}" for r in ratios),
        )
        report.row(
            "height vs sprite",
            "PASS" if height_ok else "FAIL",
            f"vertical spans within ±{C.HEIGHT_TOLERANCE_SIM:.0f} sim",
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


def apply_checklist(report, glb_path, meta, mesh):
    """Enforce the model-style checklist. Numbers come from conventions.py."""
    gltf = mesh["gltf"]
    object_id = _object_id(meta, glb_path)
    size_name = _size_class_name(meta, object_id)
    budget = C.SIZE_CLASSES[size_name]
    canopy = _is_canopy(meta, object_id, size_name)

    def mark(name, ok, detail, warn=False):
        if ok:
            report.row(name, "PASS", detail)
        elif warn:
            report.row(name, "WARN", detail)
        else:
            report.row(name, "FAIL", detail)

    safe = _identifier_safe(object_id)
    named = os.path.basename(glb_path) == f"{object_id}.glb"
    mark("file name", safe and named, object_id if safe and named else f"{os.path.basename(glb_path)} as {object_id}")

    nodes = gltf.get("nodes") or []
    meshes = gltf.get("meshes") or []
    materials = gltf.get("materials") or []
    primitives = meshes[0].get("primitives") if len(meshes) == 1 else []
    mark("one node", len(nodes) == 1, f"{len(nodes)} nodes")
    mark("one mesh", len(meshes) == 1, f"{len(meshes)} meshes")
    mark("one primitive", len(primitives) == 1, f"{len(primitives)} primitives")
    mark("one material", len(materials) == 1, f"{len(materials)} materials")
    for key in ("animations", "skins", "cameras", "lights"):
        if gltf.get(key):
            report.row(key, "FAIL", "not allowed on a scenery model")
    if len(nodes) == 1:
        before = len(report.errors)
        _node_is_identity(nodes[0], report, nodes[0].get("name", "node"))
        mark("identity transform", len(report.errors) == before, "node TRS is identity" if len(report.errors) == before else "see FAIL above")
    extras = (gltf.get("extras") or {}).get("openrct2_3d") or {}
    mark("no baked Z squash", extras.get("z_squash") is not True, "render squash stays out of the file")

    prim = primitives[0] if len(primitives) == 1 else {}
    attrs = set((prim.get("attributes") or {}))
    mark(
        "attributes",
        attrs == {"POSITION", "NORMAL", "TEXCOORD_0"},
        "POSITION NORMAL TEXCOORD_0" if attrs == {"POSITION", "NORMAL", "TEXCOORD_0"} else str(sorted(attrs)),
    )
    index_acc = None
    if prim.get("indices") is not None and gltf.get("accessors"):
        index_acc = gltf["accessors"][prim["indices"]]
    index_type = None if index_acc is None else index_acc.get("componentType")
    mark("u16 indices", index_type == 5123, f"componentType {index_type}")
    mode = prim.get("mode", 4)
    mark("triangles", mode == 4, f"mode {mode}")

    vertices = len(mesh["positions"])
    mark("vertices <= 2500", vertices <= C.MAX_VERTICES, str(vertices))
    triangles = mesh["triangles"]
    mark("triangles <= hard cap", triangles <= budget["hard_tris"] and triangles <= C.MAX_TRIANGLES, f"{triangles} / {budget['hard_tris']}")
    floor = budget.get("tree_warn_below")
    if floor and triangles < floor:
        mark("triangle target", False, f"{triangles} is under {floor} for {size_name}", warn=True)
    else:
        detail = f"{triangles}"
        if floor:
            detail += f" (tree target {C.TREE_TRIANGLE_TARGET[0]}-{C.TREE_TRIANGLE_TARGET[1]})"
        mark("triangle target", True, detail)

    image = mesh["image"]
    image_def = (gltf.get("images") or [None])[0] or {}
    embedded = bool(image_def.get("bufferView") is not None and not image_def.get("uri"))
    mark("embedded image", embedded and image is not None, "bufferView PNG" if embedded else "external or missing")
    if image is not None:
        height, width = image.shape[:2]
        pot = width > 0 and height > 0 and width & (width - 1) == 0 and height & (height - 1) == 0
        mark("atlas <= 256 and power of two", pot and max(width, height) <= min(C.MAX_TEXTURE, budget["max_texture"]), f"{width}x{height}")
        justified = bool(_justification(meta)) or object_id == C.PINE_OBJECT_ID
        if max(width, height) > C.PREFERRED_TEXTURE:
            mark(
                "atlas above 128",
                justified,
                "reference exception" if object_id == C.PINE_OBJECT_ID else (_justification(meta) or "needs a written justification in the meta"),
                warn=not justified,
            )
        else:
            mark("atlas above 128", True, f"{width}x{height}")
        alpha = image[..., 3]
        weird = int(np.count_nonzero((alpha != 0) & (alpha != 255)))
        mark("alpha 0 or 255", weird == 0, "cut-out" if weird == 0 else f"{weird} partial texels")
        opaque = alpha == 255
        colours = {tuple(int(c) for c in px) for px in image[opaque][:, :3]}
        palette = _palette()
        foreign = colours - palette
        mark("RCT2 palette colours", not foreign, f"{len(colours)} colours" if not foreign else f"{len(foreign)} colours are not in StandardPalette")
        if mesh["uvs"] is not None:
            density = _texel_density(mesh["positions"], mesh["uvs"], mesh["indices"], width, height)
            if density is None:
                mark("texel density", True, "too few long edges to score")
            else:
                low = C.TEXEL_DENSITY * (1 - C.TEXEL_DENSITY_TOLERANCE)
                high = C.TEXEL_DENSITY * (1 + C.TEXEL_DENSITY_TOLERANCE)
                mark("texel density", low <= density <= high, f"{density:.1f} texels/unit (target {C.TEXEL_DENSITY:.0f} ±25%)")

    if len(materials) == 1:
        before = len(report.errors)
        check_materials(gltf, report)
        # check_materials appends errors directly. Fold the outcome into one row
        # without double-counting: drop the ones it just added and record a row.
        added = report.errors[before:]
        del report.errors[before:]
        mark("material MASK 0.5 / nearest / clamp / double-sided", not added, "ok" if not added else "; ".join(added))

    bmin, bmax, _blender = _blender_bounds(mesh["positions"])
    limit = C.TREE_FOOTPRINT if canopy else C.PROP_FOOTPRINT
    horizontal = max(abs(float(bmin[0])), abs(float(bmax[0])), abs(float(bmin[1])), abs(float(bmax[1])))
    mark("footprint", horizontal <= limit + 1e-3, f"{horizontal:.3f} within ±{limit} ({'canopy' if canopy else 'prop'})")
    mark("origin inside footprint", bool(bmin[0] < 0 < bmax[0] and bmin[1] < 0 < bmax[1]), "tile centre")
    sink = -float(bmin[2])
    mark("base sink <= 0.1", sink <= C.BASE_SINK_MAX + 1e-4 and float(bmin[2]) <= 0.02, f"z min {bmin[2]:.4f}")
    declared = _height_sim(meta)
    tip = float(bmax[2]) * C.SIM_PER_UNIT
    if declared is None:
        mark("height vs meta", False, "meta has no height")
    else:
        mark("height vs meta", abs(tip - declared) <= C.HEIGHT_TOLERANCE_SIM, f"tip {tip:.2f} sim, meta {declared:.2f}")

    size_bytes = os.path.getsize(glb_path)
    mark("GLB <= 128 KB", size_bytes <= C.MAX_GLB_BYTES, f"{size_bytes} bytes")
    if size_bytes <= C.MAX_GLB_BYTES and size_bytes > C.TARGET_GLB_BYTES:
        mark("GLB <= 100 KB target", False, f"{size_bytes} bytes", warn=True)
    else:
        mark("GLB <= 100 KB target", size_bytes <= C.TARGET_GLB_BYTES, f"{size_bytes} bytes")
    report.note(
        f"bounds min [{bmin[0]:.4f}, {bmin[1]:.4f}, {bmin[2]:.4f}] "
        f"max [{bmax[0]:.4f}, {bmax[1]:.4f}, {bmax[2]:.4f}]  class {size_name}"
    )


def validate_files(glb_path, meta_path, sprites_dir, out_path, require_iou):
    report = Report()
    if not os.path.isfile(glb_path):
        report.error(f"GLB not found: {glb_path}")
        return report
    if not os.path.isfile(meta_path):
        report.error(f"meta not found: {meta_path}")
        return report
    meta = _load_json(meta_path)
    try:
        mesh = load_mesh(glb_path)
    except Exception as exc:
        report.error(f"could not read GLB: {exc}")
        return report
    if mesh["gltf"].get("asset", {}).get("version") != "2.0":
        report.row("glTF 2.0", "FAIL", str(mesh["gltf"].get("asset")))
    apply_checklist(report, glb_path, meta, mesh)
    render_and_score(mesh, meta, sprites_dir, out_path, report, require_iou)
    return report


def _print_report(report, title=None):
    if title:
        print(title)
    if report.rows:
        width = max(len(name) for name, _status, _detail in report.rows)
        for name, status, detail in report.rows:
            extra = f"  {detail}" if detail else ""
            print(f"  {status:<4}  {name:<{width}}{extra}")
    for line in report.notes:
        print(f"  note  {line}")
    if report.ok():
        print(f"validator: pass ({len(report.warnings)} warnings)")
    else:
        print(f"validator: fail ({len(report.errors)} errors, {len(report.warnings)} warnings)")


def _git_show(ref, path):
    try:
        return subprocess.check_output(["git", "show", f"{ref}:{path}"], cwd=REPO_ROOT, stderr=subprocess.DEVNULL)
    except subprocess.CalledProcessError:
        return None


def _approved_pair():
    """PR #6's flat pine, if this checkout has it or the branch ref is present."""
    if os.path.isfile(FLAT_GLB) and os.path.isfile(FLAT_META):
        return FLAT_GLB, FLAT_META
    ref = "origin/cursor/scenery-glb-milestone-1ca6"
    glb = _git_show(ref, f"data/object-models/{PINE_ID}.glb")
    meta = _git_show(ref, f"data/object-models/{PINE_ID}.meta.json")
    if glb is None or meta is None:
        return None
    folder = tempfile.mkdtemp(prefix="approved-pine-")
    glb_path = os.path.join(folder, PINE_ID + ".glb")
    meta_path = os.path.join(folder, PINE_ID + ".meta.json")
    open(glb_path, "wb").write(glb)
    open(meta_path, "wb").write(meta)
    return glb_path, meta_path


def self_test():
    """Build the pine with this pipeline and validate it, then the approved GLB."""
    import test_rct2dat

    test_rct2dat.run()
    error = pixels_match_rct()
    if error > 0.05:
        print(f"FAIL  projection  camera vs RCT formula error {error}")
        return Report()
    print(f"PASS  projection  max error {error:.3e} px")
    from rebuild import main as rebuild_main

    out_dir = os.path.join(TOOL_DIR, "out")
    code = rebuild_main(
        [
            "--object",
            PINE_ID,
            "--no-blender",
            "--palette-header",
            "--out",
            out_dir,
            "--skip-validate",
        ]
    )
    if code:
        print("FAIL  pipeline build")
        failed = Report()
        failed.error("rebuild failed")
        return failed
    pipeline = validate_files(
        os.path.join(out_dir, PINE_ID + ".glb"),
        os.path.join(out_dir, PINE_ID + ".meta.json"),
        None,
        os.path.join(out_dir, PINE_ID + ".review.png"),
        require_iou=False,
    )
    _print_report(pipeline, "\npipeline pine (Python writer, gitignored out/)")
    approved = _approved_pair()
    if approved is None:
        print(
            "\nSKIP  approved GLB  data/object-models/"
            + PINE_ID
            + ".glb is not in this checkout (it lands with PR #6)"
        )
        approved_report = Report()
    else:
        approved_report = validate_files(approved[0], approved[1], None, None, require_iou=False)
        _print_report(approved_report, "\nPR #6 approved GLB (Blender 4.2 export)")
    if pipeline.ok() and approved_report.ok():
        return pipeline
    failed = Report()
    failed.errors.extend(pipeline.errors)
    failed.errors.extend(approved_report.errors)
    return failed


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
        if args.model_dir.endswith(".glb"):
            args.glb = args.model_dir
            args.meta = args.model_dir[: -len(".glb")] + ".meta.json"
        elif os.path.isdir(args.model_dir):
            # Flat directory: object-models/<id>.glb beside <id>.meta.json. Not recursive.
            metas = [n for n in os.listdir(args.model_dir) if n.endswith(".meta.json")]
            if len(metas) != 1:
                print("pass a .glb path, or a directory with exactly one .meta.json", file=sys.stderr)
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
