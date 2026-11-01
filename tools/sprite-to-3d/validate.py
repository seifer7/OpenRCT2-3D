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
import sys

import numpy as np

import conventions as C
from glb_io import gltf_to_blender, load_mesh
from projection import pixels_match_rct
from render_views import (
    closed_silhouette_iou,
    load_sprite_folder,
    place_sprite,
    rasterize,
    silhouette_iou,
    write_review,
)

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


def triangle_justification(meta):
    """The meta's written reason for standing over the soft triangle cap, or ""."""
    value = meta.get("triangle_justification")
    if value is None:
        value = (meta.get("geometry") or {}).get("triangle_justification")
    return str(value).strip() if value and str(value).strip() else ""


def _fallback_budget():
    """Budget for a meta whose size_class is missing or unknown (that row already fails)."""
    return {
        "hard_tris": C.MAX_TRIANGLES,
        "soft_max_tris": None,
        "tree_warn_below": None,
        "max_texture": C.MAX_TEXTURE,
        "min_render_px": C.DEFAULT_MIN_RENDER_PX,
        "footprint_bu": C.PROP_FOOTPRINT,
        "trunk_base_bu": None,
        "canopy": False,
    }


def min_render_px(meta):
    """Opaque-pixel floor per view for the meta's size class (conventions.SIZE_CLASSES)."""
    budget = C.SIZE_CLASSES.get(meta.get("size_class")) or _fallback_budget()
    return int(budget.get("min_render_px") or C.DEFAULT_MIN_RENDER_PX)


def trunk_base_extent(positions, indices=None, band_sim=None):
    """Max |x| and |y| (tile units) of the trunk base, the bottom slice of the mesh.

    The slice runs from the lowest vertex up to band_sim (default
    conventions.TRUNK_BASE_BAND_SIM) above it. Vertices inside the slice count.
    With indices, so does every point where a mesh edge crosses the top of the
    slice, so a long trunk triangle that starts in the slice is measured where
    it leaves it. positions are glTF Y-up.
    """
    band = (C.TRUNK_BASE_BAND_SIM if band_sim is None else float(band_sim)) / C.SIM_PER_UNIT
    _bmin, _bmax, blender = _blender_bounds(positions)
    top = float(blender[:, 2].min()) + band
    inside = blender[blender[:, 2] <= top + 1e-9]
    extent = float(np.abs(inside[:, :2]).max()) if len(inside) else 0.0
    if indices is None:
        return extent
    idx = np.asarray(indices, np.int64).reshape(-1, 3)
    for a, b in ((0, 1), (1, 2), (2, 0)):
        za = blender[idx[:, a], 2] - top
        zb = blender[idx[:, b], 2] - top
        cross = ((za < 0) & (zb > 0)) | ((za > 0) & (zb < 0))
        if not np.any(cross):
            continue
        pa = blender[idx[cross, a]]
        pb = blender[idx[cross, b]]
        t = (za[cross] / (za[cross] - zb[cross]))[:, None]
        hit = pa + t * (pb - pa)
        extent = max(extent, float(np.abs(hit[:, :2]).max()))
    return extent


def _size_class_budget(meta):
    """Return (name, budget) or (None, None). Canopy comes only from the class table."""
    name = meta.get("size_class")
    if name not in C.SIZE_CLASSES:
        return None, None
    return name, C.SIZE_CLASSES[name]


def alignment_of(meta):
    """Return (shift_xy, y_nudge) or None when the meta does not declare alignment.

    There is no default. A missing block used to paste sprites at shift (0, 0)
    and the pine's IoU fell from 0.725 to about 0.67.
    """
    alignment = meta.get("alignment")
    if not isinstance(alignment, dict):
        return None
    shift = alignment.get("sprite_shift")
    nudge = alignment.get("sprite_y_nudge")
    if not isinstance(shift, (list, tuple)) or len(shift) != 2 or nudge is None:
        return None
    return [float(shift[0]), float(shift[1])], float(nudge)


def tip_height_sim(mask, anchor_y):
    """Sim height of the top opaque row above the z=0 anchor row.

    The comparison rasteriser applies the render-only Z squash, so one pixel
    in this image is already one sim Z unit. Do not scale by 1/sqrt(2/3) again.
    The mask bottom is ignored: a trunk sunk below z=0 must not add to the tip.
    """
    image = np.asarray(mask)
    opaque = image[..., 3] > 0 if image.ndim == 3 else image.astype(bool)
    rows = np.nonzero(opaque.any(axis=1))[0]
    if len(rows) == 0:
        return None
    return float(anchor_y) - float(rows[0])


def sprite_anchor_y(shift, nudge):
    """Row where the sprite's draw anchor lands after the fitted paste."""
    return C.ORIGIN_PIXEL[1] + float(nudge) - float(shift[1])


def compare_tip_heights(render_masks, sprite_masks, shift, nudge):
    """Model tip against the tallest sprite tip, both above their anchor rows.

    Returns ok, model_tip, tallest_sprite_tip, per-view (sprite_tip, render_tip, delta).
    delta is render tip minus sprite tip, in sim units.
    """
    render_y = C.ORIGIN_PIXEL[1]
    sprite_y = sprite_anchor_y(shift, nudge)
    rows = []
    for view in range(4):
        sprite_tip = tip_height_sim(sprite_masks[view], sprite_y)
        render_tip = tip_height_sim(render_masks[view], render_y)
        if sprite_tip is None or render_tip is None:
            return False, None, None, rows
        rows.append((sprite_tip, render_tip, render_tip - sprite_tip))
    model_tip = max(item[1] for item in rows)
    tallest_sprite = max(item[0] for item in rows)
    ok = abs(model_tip - tallest_sprite) <= C.HEIGHT_TOLERANCE_SIM
    return ok, model_tip, tallest_sprite, rows


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


def per_view_tip_status(deltas):
    """Gate per-view tip deltas (render minus sprite, sim units).

    Any view over TIP_VIEW_FAIL_SIM fails; any over TIP_VIEW_WARN_SIM warns.
    Returns (status, detail).
    """
    worst = max(deltas, key=abs)
    text = ", ".join(f"{d:+.1f}" for d in deltas)
    if any(abs(d) > C.TIP_VIEW_FAIL_SIM for d in deltas):
        return "FAIL", f"views {text} sim; worst {worst:+.1f} is beyond ±{C.TIP_VIEW_FAIL_SIM:.0f}"
    if any(abs(d) > C.TIP_VIEW_WARN_SIM for d in deltas):
        return "WARN", f"views {text} sim; worst {worst:+.1f} is beyond ±{C.TIP_VIEW_WARN_SIM:.0f}"
    return "PASS", f"views {text} sim (±{C.TIP_VIEW_WARN_SIM:.0f})"


def score_silhouettes(renders, placed, kernel=None):
    """Raw and closed-mask IoU per view, plus the pixel counts.

    renders and placed are the four model renders and the four pasted sprites.
    The closed score closes BOTH masks with the same kernel (conventions.IOU_CLOSE_KERNEL).
    """
    kernel = C.IOU_CLOSE_KERNEL if kernel is None else int(kernel)
    raw, closed, sprite_px, render_px = [], [], [], []
    for view in range(4):
        score, s_px, r_px = silhouette_iou(placed[view], renders[view])
        raw.append(float(score))
        closed.append(float(closed_silhouette_iou(placed[view], renders[view], kernel)))
        sprite_px.append(s_px)
        render_px.append(r_px)
    return {
        "raw": {"views": raw, "mean": float(np.mean(raw))},
        "closed": {"views": closed, "mean": float(np.mean(closed)), "kernel_px": kernel},
        "sprite_px": sprite_px,
        "render_px": render_px,
    }


def measure_iou(mesh, meta, sprites_dir):
    """Render the mesh, paste the local sprites with the meta alignment, and score.

    Returns score_silhouettes(...) or None when the meta has no alignment.
    Used by rebuild.py to record raw and closed IoU in the meta.
    """
    alignment = alignment_of(meta)
    if alignment is None:
        return None
    shift, nudge = alignment
    renders = [rasterize(mesh["positions"], mesh["uvs"], mesh["indices"], mesh["image"], v) for v in range(4)]
    placed = [place_sprite(s, shift, nudge) for s in load_sprite_folder(sprites_dir)]
    return score_silhouettes(renders, placed)


def _hist_paths(value, path=""):
    """Paths of palette-histogram keys (hist, *_hist) anywhere in a JSON value."""
    found = []
    if isinstance(value, dict):
        for key, item in value.items():
            here = f"{path}.{key}" if path else str(key)
            if key == "hist" or str(key).endswith("_hist"):
                found.append((here, item))
            found.extend(_hist_paths(item, here))
    elif isinstance(value, list):
        for i, item in enumerate(value):
            found.extend(_hist_paths(item, f"{path}[{i}]"))
    return found


def render_and_score(mesh, meta, sprites_dir, out_path, report, require_iou):
    renders = []
    for view in range(4):
        renders.append(rasterize(mesh["positions"], mesh["uvs"], mesh["indices"], mesh["image"], view))
    coverage = [int(np.count_nonzero(im[..., 3] > 0)) for im in renders]
    report.note("render coverage " + ", ".join(str(c) for c in coverage))
    # Empty-render guard. The floor is per size class (min_render_px in
    # conventions.SIZE_CLASSES): quarter-tile objects draw about 200 px a view.
    floor = min_render_px(meta)
    for view, count in enumerate(coverage):
        if count < floor:
            report.error(
                f"view {view} rendered {count} opaque pixels (floor {floor} for "
                f"{meta.get('size_class')}); the projection or the alpha cut is empty"
            )
    placed = None
    ious = None
    if sprites_dir:
        alignment = alignment_of(meta)
        if alignment is None:
            report.row(
                "alignment",
                "FAIL",
                "meta.alignment.sprite_shift and sprite_y_nudge are required when --sprites is set; "
                "there is no default paste",
            )
            if out_path:
                write_review(out_path, renders, None)
                report.note(f"wrote {out_path}")
            return None
        shift, nudge = alignment
        loaded = load_sprite_folder(sprites_dir)
        placed = [place_sprite(s, shift, nudge) for s in loaded]
        scores = score_silhouettes(renders, placed)
        ious = scores["raw"]["views"]
        closed = scores["closed"]["views"]
        for view in range(4):
            report.note(
                f"view {view} IoU raw {ious[view]:.3f}, closed {closed[view]:.3f} "
                f"(sprite {scores['sprite_px'][view]}px, render {scores['render_px'][view]}px)"
            )
        mean = scores["raw"]["mean"]
        closed_mean = scores["closed"]["mean"]
        report.note(f"mean IoU raw {mean:.3f}, closed {closed_mean:.3f}")
        iou_ok = all(s >= C.IOU_CLOSED_MIN_PER_VIEW for s in closed) and closed_mean >= C.IOU_CLOSED_MIN_MEAN
        report.row(
            f"silhouette IoU (closed {C.IOU_CLOSE_KERNEL}px)",
            "PASS" if iou_ok else "FAIL",
            f"views {[round(s, 3) for s in closed]} mean {closed_mean:.3f} "
            f"(gate: each >= {C.IOU_CLOSED_MIN_PER_VIEW:.2f}, mean >= {C.IOU_CLOSED_MIN_MEAN:.2f})",
        )
        # Raw IoU is reported, not gated, so a stippled sprite stays visible.
        raw_low = any(s < C.IOU_MIN_PER_VIEW for s in ious) or mean < C.IOU_MIN_MEAN
        report.row(
            "silhouette IoU (raw, reported)",
            "INFO",
            f"views {[round(s, 3) for s in ious]} mean {mean:.3f}"
            + (f" (under the old raw bar {C.IOU_MIN_PER_VIEW:.2f}/{C.IOU_MIN_MEAN:.2f})" if raw_low else ""),
        )
        ratios = []
        for view in range(4):
            sprite_px = int(np.count_nonzero(placed[view][..., 3] > 0))
            render_px = int(np.count_nonzero(renders[view][..., 3] > 0))
            ratios.append(render_px / sprite_px if sprite_px else 0)
        ratio_ok = all(C.AREA_RATIO[0] <= r <= C.AREA_RATIO[1] for r in ratios)
        report.row(
            "area ratio",
            "PASS" if ratio_ok else "FAIL",
            ", ".join(f"{r:.2f}" for r in ratios),
        )
        ok, model_tip, tallest, rows = compare_tip_heights(renders, placed, shift, nudge)
        for view, (sprite_tip, render_tip, delta) in enumerate(rows):
            report.note(
                f"view {view} tip above anchor: sprite {sprite_tip:.1f} sim, "
                f"render {render_tip:.1f} sim, delta {delta:+.1f}"
            )
        if model_tip is None:
            report.row("height vs sprite", "FAIL", "a view has an empty silhouette")
        else:
            report.row(
                "height vs sprite",
                "PASS" if ok else "FAIL",
                f"model tip {model_tip:.1f} sim vs tallest sprite tip {tallest:.1f} sim "
                f"(±{C.HEIGHT_TOLERANCE_SIM:.0f}; squash already in the render)",
            )
            status, detail = per_view_tip_status([row[2] for row in rows])
            report.row("per-view tip delta", status, detail)
        declared = ((meta.get("iou") or {}).get("closed") or {}).get("views")
        if declared and len(declared) == 4:
            # Stored figures are a recorded measurement. A fresh rasteriser will not
            # hit them exactly; this only catches a meta that claims a pass it didn't earn.
            if any(float(v) < C.IOU_CLOSED_MIN_PER_VIEW for v in declared):
                report.error("meta iou.closed.views records a view under the closed-mask threshold")
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
    size_name, budget = _size_class_budget(meta)
    if budget is None:
        known = ", ".join(sorted(C.SIZE_CLASSES))
        report.row("size_class", "FAIL", f"{meta.get('size_class')!r} is missing or not one of: {known}")
        budget = _fallback_budget()
        canopy = False
    else:
        report.row("size_class", "PASS", size_name)
        canopy = bool(budget["canopy"])

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
    soft = budget.get("soft_max_tris")
    if soft is not None:
        # Over the soft cap warns (exit code unaffected); over the hard cap fails above.
        # A triangle_justification in the meta annotates the warning; it stays a WARN.
        over = triangles > soft
        detail = f"{triangles} is over the {size_name} soft cap of {soft}" if over else f"{triangles} / {soft}"
        reason = triangle_justification(meta)
        if over and reason:
            detail += f"; justified: {reason}"
        mark("triangles <= soft cap", not over, detail, warn=True)
    floor = budget.get("tree_warn_below")
    if floor and triangles < floor:
        mark("triangle target", False, f"{triangles} is under {floor} for {size_name}", warn=True)
    else:
        detail = f"{triangles}"
        if floor:
            detail += f" (tree floor {floor}, soft cap {budget.get('soft_max_tris')})"
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
    limit = budget.get("footprint_bu")
    if limit is None:
        limit = C.TREE_FOOTPRINT if canopy else C.PROP_FOOTPRINT
    horizontal = max(abs(float(bmin[0])), abs(float(bmax[0])), abs(float(bmin[1])), abs(float(bmax[1])))
    kind = "canopy" if canopy or budget.get("trunk_base_bu") else "whole mesh"
    mark("footprint", horizontal <= limit + 1e-3, f"{horizontal:.3f} within ±{limit} ({kind}, {size_name})")
    base_limit = budget.get("trunk_base_bu")
    if base_limit is not None:
        base = trunk_base_extent(mesh["positions"], mesh.get("indices"))
        mark(
            "trunk base footprint",
            base <= base_limit + 1e-3,
            f"{base:.3f} within ±{base_limit} (bottom {C.TRUNK_BASE_BAND_SIM:.0f} sim of the mesh)",
        )
    mark("origin inside footprint", bool(bmin[0] < 0 < bmax[0] and bmin[1] < 0 < bmax[1]), "tile centre")
    sink = -float(bmin[2])
    mark("base sink <= 0.1", sink <= C.BASE_SINK_MAX + 1e-4 and float(bmin[2]) <= 0.02, f"z min {bmin[2]:.4f}")
    declared = _height_sim(meta)
    tip = float(bmax[2]) * C.SIM_PER_UNIT
    if declared is None:
        mark("height vs meta", False, "meta has no height")
    else:
        mark("height vs meta", abs(tip - declared) <= C.HEIGHT_TOLERANCE_SIM, f"tip {tip:.2f} sim, meta {declared:.2f}")

    params = meta.get("params")
    stored = _hist_paths(params) if params is not None else []
    mark(
        "no palette histograms in meta",
        not stored,
        "params carry none" if not stored else "copied from params.json: " + ", ".join(p for p, _v in stored[:4]),
    )

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


def _approved_pair():
    """The flat pine on this checkout. Nothing is fetched from another branch."""
    if os.path.isfile(FLAT_GLB) and os.path.isfile(FLAT_META):
        return FLAT_GLB, FLAT_META
    return None


def self_test():
    """Build the pine with this pipeline and validate it, then the approved GLB."""
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
