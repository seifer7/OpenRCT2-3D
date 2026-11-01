# Copyright (c) 2026 OpenRCT2 developers
# Licensed under the GNU General Public License version 3.

"""Rasterise a Y-up GLB at the four RCT2 views and score it against sprites.

The camera applies the render-only Z squash. The GLB is not modified.
Sprite pixels are never written outside the caller-supplied output path;
that path is gitignored (see the repository .gitignore).
"""

from __future__ import annotations

import json
import os

import numpy as np
from PIL import Image, ImageDraw

from conventions import ORIGIN_PIXEL, RENDER_SIZE
from glb_io import gltf_to_blender
from projection import camera_depth, camera_pixel


def project_vertices(positions, view):
    out = np.empty((len(positions), 2), np.float64)
    for i, point in enumerate(positions):
        out[i] = camera_pixel(gltf_to_blender(point), view)
    return out


def vertex_depths(positions, view):
    """Camera depth per vertex (smaller is nearer), see projection.camera_depth."""
    out = np.empty(len(positions), np.float64)
    for i, point in enumerate(positions):
        out[i] = camera_depth(gltf_to_blender(point), view)
    return out


def rasterize(positions, uvs, indices, image, view):
    """Return an RGBA image. Texels with alpha <= 127 are holes (MASK 0.5).

    Depth-tested: each pixel keeps the nearest opaque texel. Camera depth is
    interpolated per pixel with the same barycentrics as the UVs (exact for
    this orthographic camera) and kept in a z-buffer, so the colours in the
    review sheet do not depend on triangle order. Which pixels are opaque does
    not depend on the depth test, so silhouettes and IoU are unchanged by it.
    """
    width, height = RENDER_SIZE
    colour = np.zeros((height, width, 4), np.uint8)
    zbuffer = np.full((height, width), np.inf, np.float64)
    pixels = project_vertices(positions, view)
    depths = vertex_depths(positions, view)
    atlas = image
    ah, aw = atlas.shape[:2] if atlas is not None else (1, 1)
    for t in range(0, len(indices), 3):
        i0, i1, i2 = (int(i) for i in indices[t : t + 3])
        p0, p1, p2 = pixels[i0], pixels[i1], pixels[i2]
        minx = max(int(np.floor(min(p0[0], p1[0], p2[0]))), 0)
        maxx = min(int(np.ceil(max(p0[0], p1[0], p2[0]))), width - 1)
        miny = max(int(np.floor(min(p0[1], p1[1], p2[1]))), 0)
        maxy = min(int(np.ceil(max(p0[1], p1[1], p2[1]))), height - 1)
        if minx > maxx or miny > maxy:
            continue
        xs = np.arange(minx, maxx + 1) + 0.5
        ys = np.arange(miny, maxy + 1) + 0.5
        gx, gy = np.meshgrid(xs, ys)
        v0 = p1 - p0
        v1 = p2 - p0
        den = v0[0] * v1[1] - v1[0] * v0[1]
        if abs(den) < 1e-8:
            continue
        v2x = gx - p0[0]
        v2y = gy - p0[1]
        b1 = (v2x * v1[1] - v1[0] * v2y) / den
        b2 = (v0[0] * v2y - v2x * v0[1]) / den
        b0 = 1.0 - b1 - b2
        inside = (b0 >= -1e-5) & (b1 >= -1e-5) & (b2 >= -1e-5)
        if not np.any(inside):
            continue
        depth = b0 * depths[i0] + b1 * depths[i1] + b2 * depths[i2]
        zsub = zbuffer[miny : maxy + 1, minx : maxx + 1]
        sub = colour[miny : maxy + 1, minx : maxx + 1]
        if uvs is None:
            draw = inside & (depth < zsub)
            sub[draw] = (180, 180, 180, 255)
            zsub[draw] = depth[draw]
            continue
        uv = b0[..., None] * uvs[i0] + b1[..., None] * uvs[i1] + b2[..., None] * uvs[i2]
        ix = np.clip((uv[..., 0] * aw).astype(np.int32), 0, aw - 1)
        iy = np.clip((uv[..., 1] * atlas.shape[0]).astype(np.int32), 0, ah - 1)
        sample = atlas[iy, ix]
        # MASK holes neither draw nor write depth, so a card's cut-out
        # shows whatever is behind it.
        draw = inside & (sample[..., 3] > 127) & (depth < zsub)
        sub[draw] = sample[draw]
        zsub[draw] = depth[draw]
    return colour


def load_sprite_folder(path):
    """Load 0.png..3.png and offsets.json from an extracted object folder."""
    offsets_path = os.path.join(path, "offsets.json")
    if not os.path.isfile(offsets_path):
        raise FileNotFoundError(f"no offsets.json in {path}")
    with open(offsets_path, encoding="utf-8") as fh:
        meta = json.load(fh)
    by_index = {int(s["index"]): s for s in meta["sprites"]}
    sprites = []
    for index in range(4):
        if index not in by_index:
            raise FileNotFoundError(f"{path} has no sprite {index}")
        image = np.array(Image.open(os.path.join(path, f"{index}.png")).convert("RGBA"))
        entry = by_index[index]
        sprites.append(
            {
                "index": index,
                "image": image,
                "x_offset": int(entry["x_offset"]),
                "y_offset": int(entry["y_offset"]),
                "width": int(entry["width"]),
                "height": int(entry["height"]),
            }
        )
    return sprites


def place_sprite(sprite, shift, y_nudge):
    """Paste a sprite so its anchor lines up with the model origin.

    shift is the fitted extra (the pine uses [-2, 0]). y_nudge is the
    full-tile anchor correction: painter anchor (15, 15) versus the tile
    centre (16, 16) is one pixel up, so the default nudge is -1.
    """
    width, height = RENDER_SIZE
    canvas = np.zeros((height, width, 4), np.uint8)
    image = sprite["image"]
    x0 = int(round(sprite["x_offset"] + ORIGIN_PIXEL[0] - shift[0]))
    y0 = int(round(sprite["y_offset"] + ORIGIN_PIXEL[1] + y_nudge - shift[1]))
    ih, iw = image.shape[:2]
    src_x0 = max(0, -x0)
    src_y0 = max(0, -y0)
    dst_x0 = max(0, x0)
    dst_y0 = max(0, y0)
    dst_x1 = min(width, x0 + iw)
    dst_y1 = min(height, y0 + ih)
    if dst_x0 >= dst_x1 or dst_y0 >= dst_y1:
        return canvas
    src = image[src_y0 : src_y0 + (dst_y1 - dst_y0), src_x0 : src_x0 + (dst_x1 - dst_x0)]
    canvas[dst_y0:dst_y1, dst_x0:dst_x1] = src
    return canvas


def silhouette_iou(sprite_rgba, render_rgba):
    a = sprite_rgba[..., 3] > 0
    b = render_rgba[..., 3] > 0
    inter = int(np.count_nonzero(a & b))
    union = int(np.count_nonzero(a | b))
    return (inter / union if union else 0.0), int(np.count_nonzero(a)), int(np.count_nonzero(b))


def _shift_mask(mask, dy, dx):
    out = np.zeros_like(mask)
    h, w = mask.shape
    out[max(dy, 0) : h + min(dy, 0), max(dx, 0) : w + min(dx, 0)] = mask[
        max(-dy, 0) : h + min(-dy, 0), max(-dx, 0) : w + min(-dx, 0)
    ]
    return out


def close_mask(mask, kernel):
    """Morphological close (dilate, then erode) with a square kernel x kernel element.

    The mask is padded with background first, so the close behaves as if the
    canvas were unbounded. kernel 3 is the 3x3 ("3px") close; 1 is a no-op.
    """
    mask = np.asarray(mask, bool)
    r = (int(kernel) - 1) // 2
    if r <= 0:
        return mask.copy()
    pad = r + 2
    padded = np.pad(mask, pad)
    dilated = padded.copy()
    for dy in range(-r, r + 1):
        for dx in range(-r, r + 1):
            dilated |= _shift_mask(padded, dy, dx)
    eroded = dilated.copy()
    for dy in range(-r, r + 1):
        for dx in range(-r, r + 1):
            eroded &= _shift_mask(dilated, dy, dx)
    return eroded[pad:-pad, pad:-pad]


def closed_silhouette_iou(sprite_rgba, render_rgba, kernel):
    """IoU after closing both masks with the same kernel (QA decision 1)."""
    a = close_mask(sprite_rgba[..., 3] > 0, kernel)
    b = close_mask(render_rgba[..., 3] > 0, kernel)
    union = int(np.count_nonzero(a | b))
    return int(np.count_nonzero(a & b)) / union if union else 0.0


def overlap_image(sprite_rgba, render_rgba):
    a = sprite_rgba[..., 3] > 0
    b = render_rgba[..., 3] > 0
    out = np.zeros_like(sprite_rgba)
    out[a & b] = (60, 170, 60, 255)
    out[a & ~b] = (220, 40, 40, 255)
    out[~a & b] = (50, 90, 230, 255)
    return out


def write_review(path, renders, sprites=None):
    """Stack the four views. sprites, when given, adds the sprite and the overlap.

    The file can contain original sprites. Callers must write it under a
    gitignored directory (tools/sprite-to-3d/out) or a *.review.png name.
    """
    scale = 2
    width, height = RENDER_SIZE
    tw, th = width * scale, height * scale
    columns = 3 if sprites else 1
    pad = 8
    sheet = Image.new("RGBA", (columns * tw + (columns + 1) * pad, 4 * (th + pad + 18) + pad), (40, 44, 52, 255))
    draw = ImageDraw.Draw(sheet)
    labels_model = ["model view 0", "model view 1", "model view 2", "model view 3"]
    for view, render in enumerate(renders):
        row_y = pad + view * (th + pad + 18)
        cells = [render]
        captions = [labels_model[view]]
        if sprites is not None:
            cells = [sprites[view], render, overlap_image(sprites[view], render)]
            captions = [f"sprite view {view}", labels_model[view], "overlap (green both, red sprite, blue model)"]
        for col, (cell, caption) in enumerate(zip(cells, captions)):
            x = pad + col * (tw + pad)
            image = Image.fromarray(cell, "RGBA").resize((tw, th), Image.NEAREST)
            sheet.paste(Image.new("RGBA", image.size, (90, 110, 90, 255)), (x, row_y + 18))
            sheet.alpha_composite(image, (x, row_y + 18))
            draw.text((x, row_y + 2), caption, fill=(255, 255, 255, 255))
    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    sheet.save(path)
    return path
