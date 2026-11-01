# Copyright (c) 2026 OpenRCT2 developers
# Licensed under the GNU General Public License version 3.

"""Pixel-art atlas for the prop archetype.

Same method as conifer_atlas: each tile is painted as a shade field, the
shades are histogram-matched to palette-index counts taken from the object's
own sprite, and the indices are coloured with the caller's palette (Data/g1.dat).
Alpha is only 0 or 255. Keys inside one histogram are ordered by palette
brightness, so the darkest shade gets the darkest index even when a
histogram mixes two ramps (for example 106-109 and 214-219 on a barrel).

params["atlas"] = {"size": [64, 64], "seed": 7, "tiles": [tile, ...]} where
each tile is {"name", "rect": [x, y, w, h], "kind", "hist": {index: count}}
plus kind options:

  planks  vertical staves: plank (width in texels), grad (top-to-bottom
          darkening), seam (darkest index on plank joins), bands: list of
          {"v": [v0, v1] fractions of the tile height, "hist": {...}} for hoops.
  lid     disc of planks across the tile with a rim ring: rim (texels), rim_hist.
  metal   turned metal: grad, bands as for planks, streak (highlight strength).
  grain   wood with streaks along u: grad.
  wheel   cut-out spoked wheel: spokes, rim (texels), hub (texels), rim_hist,
          hub_hist; texels between spokes are alpha 0 unless fill_hist
          is set, which paints them as a solid (darker) wheel face.
  solid   noise only.

No sprite pixels are copied. Only the counts in "hist" come from the sprite.
"""

from __future__ import annotations

import json

import numpy as np
from PIL import Image

DEFAULT_SIZE = (64, 64)
DEFAULT_SEED = 4099


def _hist(h):
    return {int(k): int(v) for k, v in (h or {}).items() if int(v) > 0}


def _luma(palette, idx):
    r, g, b = (float(c) for c in palette[idx][:3])
    return 0.299 * r + 0.587 * g + 0.114 * b


def match(shade, mask, hist, palette):
    """Assign palette indices to masked texels so their counts follow hist."""
    out = np.zeros(shade.shape, np.int32)
    if not np.any(mask) or not hist:
        return out
    keys = sorted(hist, key=lambda k: (_luma(palette, k), k))
    cnt = np.array([hist[k] for k in keys], float)
    vals = shade[mask]
    order = np.argsort(vals, kind="stable")
    res = np.empty(vals.size, np.int32)
    cut = np.round(np.cumsum(cnt) / cnt.sum() * vals.size).astype(int)
    s0 = 0
    for key, c in zip(keys, cut):
        res[order[s0:c]] = key
        s0 = c
    out[mask] = res
    return out


def _bands(spec, h):
    rows = []
    for band in spec.get("bands") or []:
        v0, v1 = band["v"]
        y0 = int(round(v0 * h))
        y1 = max(y0 + 1, int(round(v1 * h)))
        rows.append((y0, min(y1, h), _hist(band["hist"])))
    return rows


def paint_tile(spec, palette, rng):
    w, h = int(spec["rect"][2]), int(spec["rect"][3])
    kind = spec.get("kind", "solid")
    hist = _hist(spec.get("hist"))
    yy, xx = np.mgrid[0:h, 0:w].astype(float)
    noise = rng.normal(0, 1, (h, w))
    idx = np.zeros((h, w), np.int32)
    full = np.ones((h, w), bool)
    grad = float(spec.get("grad", 0.6))

    if kind in ("planks", "metal", "grain", "solid"):
        shade = -grad * (yy / max(h - 1, 1)) + 0.25 * noise
        if kind == "planks":
            plank = max(2, int(spec.get("plank", 3)))
            pid = (xx // plank).astype(int)
            n = int(pid.max()) + 1
            base = rng.normal(0, 0.45, n)
            shade = shade + base[pid]
            # A lit edge on one side of each plank, the pixel-art stave look.
            shade = shade + 0.25 * ((xx % plank) == 0)
        elif kind == "metal":
            streak = float(spec.get("streak", 0.8))
            col = np.repeat(rng.normal(0, 0.35, (1, w)), h, 0)
            shade = shade + col + streak * np.exp(-(((xx - 0.3 * w) / (0.12 * w + 1)) ** 2))
        elif kind == "grain":
            row = np.repeat(rng.normal(0, 0.5, (h, 1)), w, 1)
            shade = shade + row + 0.2 * np.repeat(rng.normal(0, 1, (h, max(1, w // 4))), 4, 1)[:, :w]
        band_mask = np.zeros((h, w), bool)
        for y0, y1, bh in _bands(spec, h):
            m = np.zeros((h, w), bool)
            m[y0:y1] = True
            idx[m] = match(shade + 0.3 * noise, m, bh, palette)[m]
            band_mask |= m
        body = full & ~band_mask
        idx[body] = match(shade, body, hist, palette)[body]
        if kind == "planks" and spec.get("seam", True):
            plank = max(2, int(spec.get("plank", 3)))
            seam = body & ((xx % plank) == plank - 1) & (rng.random((h, w)) < 0.6)
            darkest = min(hist, key=lambda k: _luma(palette, k))
            idx[seam] = darkest
        return idx

    cx, cy = (w - 1) / 2.0, (h - 1) / 2.0
    rr = np.hypot(xx - cx, yy - cy)
    radius = min(w, h) / 2.0
    if kind == "lid":
        rim = float(spec.get("rim", 1.5))
        plank = max(2, int(spec.get("plank", 3)))
        pid = (yy // plank).astype(int)
        base = rng.normal(0, 0.45, int(pid.max()) + 1)
        shade = base[pid] + 0.25 * noise - 0.35 * (rr / radius) ** 2 + 0.2 * ((yy % plank) == 0)
        rim_mask = rr >= radius - rim
        body = ~rim_mask
        idx[body] = match(shade, body, hist, palette)[body]
        idx[rim_mask] = match(shade, rim_mask, _hist(spec.get("rim_hist")) or hist, palette)[rim_mask]
        return idx
    if kind == "wheel":
        rim = float(spec.get("rim", 1.5))
        hub = float(spec.get("hub", 1.5))
        spokes = int(spec.get("spokes", 8))
        sw = float(spec.get("spoke_width", 0.75))
        ang = np.arctan2(yy - cy, xx - cx)
        rim_mask = (rr <= radius) & (rr >= radius - rim)
        hub_mask = rr <= hub
        # Distance from the nearest spoke centre line.
        k = np.round(ang / (2 * np.pi / spokes)) * (2 * np.pi / spokes)
        dist = np.abs(rr * np.sin(ang - k))
        spoke_mask = (rr < radius - rim + 0.5) & ~hub_mask & (dist <= sw)
        shade = 0.25 * noise - 0.6 * (yy / max(h - 1, 1))
        rim_hist = _hist(spec.get("rim_hist")) or hist
        hub_hist = _hist(spec.get("hub_hist")) or hist
        fill_hist = _hist(spec.get("fill_hist"))
        if fill_hist:
            fill_mask = (rr <= radius) & ~rim_mask & ~hub_mask & ~spoke_mask
            idx[fill_mask] = match(shade, fill_mask, fill_hist, palette)[fill_mask]
        idx[rim_mask] = match(shade, rim_mask, rim_hist, palette)[rim_mask]
        idx[spoke_mask] = match(shade, spoke_mask, hist, palette)[spoke_mask]
        idx[hub_mask] = match(shade, hub_mask, hub_hist, palette)[hub_mask]
        return idx
    raise ValueError(f"unknown prop atlas tile kind {kind!r}")


def paint(params, palette):
    """Return (rgba uint8 array, tiles dict, index array)."""
    spec = params.get("atlas") or {}
    width, height = spec.get("size", list(DEFAULT_SIZE))
    seed = int(spec.get("seed", DEFAULT_SEED))
    indices = np.zeros((height, width), np.int32)
    tiles = {}
    for n, tile in enumerate(spec["tiles"]):
        x, y, w, h = (int(v) for v in tile["rect"])
        if x < 0 or y < 0 or x + w > width or y + h > height:
            raise ValueError(f"atlas tile {tile['name']} {tile['rect']} is outside {width}x{height}")
        rng = np.random.default_rng(seed + 101 * n)
        indices[y : y + h, x : x + w] = paint_tile(tile, palette, rng)
        tiles[tile["name"]] = [x, y, w, h]
    rgba = np.zeros((height, width, 4), np.uint8)
    opaque = indices > 0
    rgba[opaque, :3] = palette[indices[opaque]]
    rgba[opaque, 3] = 255
    return rgba, tiles, indices


def write_atlas(params, palette, png_path, json_path=None):
    rgba, tiles, _indices = paint(params, palette)
    Image.fromarray(rgba, "RGBA").save(png_path)
    payload = {
        "size": [int(rgba.shape[1]), int(rgba.shape[0])],
        "tiles": tiles,
        "alpha": "cutout (0 or 255), use MASK / alpha clip 0.5",
        "filter": "NEAREST, no mipmaps",
    }
    if json_path:
        with open(json_path, "w", encoding="utf-8") as fh:
            json.dump(payload, fh, indent=1)
            fh.write("\n")
    return payload
