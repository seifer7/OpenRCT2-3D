# Copyright (c) 2026 OpenRCT2 developers
# Licensed under the GNU General Public License version 3.

"""Pixel-art conifer atlas. Needle tufts are painted as palette indices, then
histogram-matched to the sprite's own colour counts and coloured with the
caller's palette. Alpha is only 0 or 255.

The tile layout and the paint order are the approved Corsican Pine atlas
(256x256). Histograms and the RNG seed come from the params dict so a second
conifer can retarget the same painter.
"""

from __future__ import annotations

import json

import numpy as np
from PIL import Image

DEFAULT_SIZE = (256, 256)
DEFAULT_SEED = 2611

# Counts measured on the Corsican Pine sprite. Keys are palette indices.
DEFAULT_TOP_HIST = {142: 90, 143: 300, 144: 400, 145: 450, 146: 360, 147: 330, 148: 160, 149: 110, 150: 25}
DEFAULT_SIDE_HIST = {142: 380, 143: 560, 144: 450, 145: 330, 146: 200, 147: 120, 148: 50, 149: 30, 150: 5}
DEFAULT_SPRITE_HIST = {142: 277, 143: 469, 144: 431, 145: 440, 146: 303, 147: 270, 148: 119, 149: 84, 150: 17}
DEFAULT_BARK_HIST = {34: 170, 35: 170, 36: 70, 37: 40, 38: 10, 11: 25, 12: 55, 13: 55, 14: 60, 15: 40}


class Canvas:
    """Shade and lump id per texel. finish() assigns palette indices."""

    def __init__(self, height, width, rng):
        self.h = height
        self.w = width
        self.rng = rng
        self.S = np.full((height, width), np.nan)
        self.L = np.zeros((height, width), np.int32)
        self.Q = np.zeros((height, width))
        self.n = 0

    def lump(self, cx, cy, rad, lightdir=(0, 1), base=0.0, squash=1.0):
        yy, xx = np.mgrid[0 : self.h, 0 : self.w]
        dy = (yy - cy) / squash
        ang = np.arctan2(dy, xx - cx)
        k = self.rng.integers(5, 8)
        ph = self.rng.random() * 6.28
        rr = rad * (1 + 0.11 * np.sin(k * ang + ph) + 0.05 * np.sin((k + 3) * ang + 2 * ph))
        d = np.hypot(xx - cx, dy)
        m = d <= rr
        q = d / np.maximum(rr, 1e-3)
        sh = ((yy - cy) * lightdir[1] + (xx - cx) * lightdir[0]) / max(rad, 1)
        v = base - 0.55 * sh - 0.35 * q * q + self.rng.normal(0, 0.025, (self.h, self.w))
        for _ in range(self.rng.integers(2, 4)):
            a = self.rng.uniform(0, 6.28)
            rq = self.rng.uniform(0.15, 0.55) * rad
            hx = cx + rq * np.cos(a) - 0.25 * rad * lightdir[0]
            hy = cy + squash * (rq * np.sin(a) - 0.3 * rad * lightdir[1])
            hb = ((xx - hx) / 1.6) ** 2 + ((yy - hy) / 1.1) ** 2 < 1.0
            v = np.where(hb, v + 0.35, v)
        self.n += 1
        self.S[m] = v[m]
        self.L[m] = self.n
        self.Q[m] = q[m]

    def finish(self, outline_dir=(0, 1), flat_bottom=0, hist=None):
        hist = hist or DEFAULT_SPRITE_HIST
        m = ~np.isnan(self.S)
        idx = np.zeros((self.h, self.w), np.int32)
        keys = sorted(hist)
        cnt = np.array([hist[k] for k in keys], float)
        vals = self.S[m]
        order = np.argsort(vals)
        out = np.empty(vals.size, np.int32)
        cut = np.round(np.cumsum(cnt) / cnt.sum() * vals.size).astype(int)
        s0 = 0
        for key, c in zip(keys, cut):
            out[order[s0:c]] = key
            s0 = c
        idx[m] = out
        ox, oy = outline_dir
        shifts = ((0, 1), (int(np.sign(ox)), 0)) if ox else ((0, 1),)
        for dx, dy in shifts:
            nb = np.zeros_like(self.L)
            ys = slice(0, self.h - dy) if dy > 0 else slice(None)
            yd = slice(dy, self.h) if dy > 0 else slice(None)
            xs = slice(0, self.w - dx) if dx > 0 else (slice(-dx, self.w) if dx < 0 else slice(None))
            xd = slice(dx, self.w) if dx > 0 else (slice(0, self.w + dx) if dx < 0 else slice(None))
            nb[ys, xs] = self.L[yd, xd]
            edge = m & (nb != self.L) & (self.Q > 0.55)
            empty = edge & (nb == 0)
            idx[edge] = np.minimum(idx[edge], 143 if dy else 144)
            idx[empty & (dy > 0)] = 142
        if flat_bottom:
            for x in range(self.w):
                ys = np.nonzero(m[:, x])[0]
                if ys.size:
                    idx[ys[-flat_bottom:], x] = 142
        return idx


def _as_int_keyed(hist):
    if hist is None:
        return None
    return {int(k): int(v) for k, v in hist.items()}


def paint(params, palette):
    """Return (rgba uint8 array, tiles dict, index array)."""
    spec = params.get("atlas") or {}
    if spec.get("layout") == "compact128":
        return paint_compact128(spec, palette)
    width, height = spec.get("size", list(DEFAULT_SIZE))
    rng = np.random.default_rng(int(spec.get("seed", DEFAULT_SEED)))
    top_hist = _as_int_keyed(spec.get("top_hist")) or DEFAULT_TOP_HIST
    side_hist = _as_int_keyed(spec.get("side_hist")) or DEFAULT_SIDE_HIST
    sprite_hist = _as_int_keyed(spec.get("sprite_hist")) or DEFAULT_SPRITE_HIST
    bark_hist = _as_int_keyed(spec.get("bark_hist")) or DEFAULT_BARK_HIST

    indices = np.zeros((height, width), np.int32)
    tiles = {}

    def tile(name, x, y, w, h, arr):
        indices[y : y + h, x : x + w] = arr
        tiles[name] = [int(x), int(y), int(w), int(h)]

    def pad_top(w, h):
        c = Canvas(h, w, rng)
        n = max(4, int(w * h / 260))
        pts = []
        while len(pts) < n:
            u, v = rng.uniform(-1, 1, 2)
            if u * u + v * v < 0.75:
                pts.append((u, v))
        pts.sort(key=lambda p: p[1])
        for u, v in pts:
            rad = rng.uniform(0.2, 0.27) * w * (1 - 0.25 * (u * u + v * v))
            c.lump((u * 0.62 + 1) * w / 2, (v * 0.62 + 1) * h / 2, rad, (0.4, 0.9), base=0.0)
        return c.finish((1, 1), hist=top_hist)

    def pad_side(w, h):
        c = Canvas(h, w, rng)
        for cyf, rlo, rhi, base in ((0.38, 4.5, 6.0, 0.12), (0.62, 4.0, 5.5, 0.0), (0.78, 3.0, 4.6, -0.12)):
            nx = int(w / (rlo + rhi) * 1.25)
            xs = np.linspace(0.05, 0.95, nx) * w + rng.uniform(-2, 2, nx)
            for x in xs:
                e = np.sin(np.pi * np.clip(x / w, 0, 1)) ** 0.4
                rad = rng.uniform(rlo, rhi) * (0.65 + 0.35 * e)
                c.lump(x, h * cyf + rng.uniform(-0.8, 0.8), rad, (0, 1), base=base, squash=0.7)
        return c.finish((0, 1), flat_bottom=0, hist=side_hist)

    for i in range(4):
        tile(f"pad_top_L{i}", 48 * i, 0, 48, 48, pad_top(48, 48))
    for i in range(4):
        tile(f"pad_top_S{i}", 192 + 32 * (i % 2), 32 * (i // 2), 32, 32, pad_top(32, 32))
    for i in range(4):
        tile(f"pad_side{i}", 0, 48 + 16 * i, 96, 16, pad_side(96, 16))
    for i in range(8):
        c = Canvas(16, 16, rng)
        for _ in range(3):
            c.lump(rng.uniform(5, 11), rng.uniform(6, 10), rng.uniform(3.5, 5), (0, 1))
        tile(f"tuft{i}", 96 + 16 * (i % 4), 48 + 16 * (i // 4), 16, 16, c.finish((0, 1), hist=sprite_hist))

    # Vertical bark streaks. Grey-blue (11-15) is sorted after the browns so it
    # forms the long highlights. The key order is the match order, not sorted().
    noise = (
        np.repeat(rng.random((1, 32)), 128, 0) * 0.7
        + np.repeat(rng.random((8, 32)), 16, 0) * 0.15
        + rng.random((128, 32)) * 0.15
    )
    order = np.argsort(noise, axis=None)
    out = np.empty(noise.size, np.int32)
    keys = [34, 35, 36, 37, 38, 15, 14, 13, 12, 11]
    if spec.get("bark_key_order"):
        keys = [int(k) for k in spec["bark_key_order"]]
    cnt = np.array([bark_hist[k] for k in keys], float)
    cut = np.round(np.cumsum(cnt) / cnt.sum() * noise.size).astype(int)
    s0 = 0
    for key, c in zip(keys, cut):
        out[order[s0:c]] = key
        s0 = c
    tile("bark", 160, 64, 32, 128, out.reshape(128, 32))
    branch = 34 + (np.repeat(rng.random((1, 16)), 64, 0) * 2.2 + rng.random((64, 16)) * 1.0).astype(int)
    tile("branch", 192, 64, 16, 64, np.clip(branch, 34, 37))

    rgba = np.zeros((height, width, 4), np.uint8)
    opaque = indices > 0
    rgba[opaque, :3] = palette[indices[opaque]]
    rgba[opaque, 3] = 255
    return rgba, tiles, indices


# ---------------------------------------------------------------------------
# compact128: the 128x128 layout for new conifers (opt in with
# atlas.layout = "compact128"). The pine never takes this path.
#
# Histogram keys are matched in shade order (palette luminance, dark to light)
# instead of numeric order, so ramps that are not one ascending run (snow
# whites 137-141 + 21 over greens 143-144, the 70-80 olive ramp, grey bark)
# shade correctly. Outline texels go to the darkest keys of the same ramp.
# Optional "speckle": {index: fraction} scatters single cone/twig pixels.
# ---------------------------------------------------------------------------

COMPACT128_TILES = {
    "pad_top_L0": (0, 0, 32, 32),
    "pad_top_L1": (32, 0, 32, 32),
    "pad_top_L2": (64, 0, 32, 32),
    "pad_top_L3": (0, 32, 32, 32),
    "pad_top_S0": (32, 32, 24, 24),
    "pad_top_S1": (56, 32, 24, 24),
    "pad_top_S2": (80, 32, 24, 24),
    "pad_top_S3": (32, 56, 24, 24),
    "tuft0": (96, 0, 16, 16),
    "tuft1": (96, 16, 16, 16),
    "tuft2": (56, 56, 16, 16),
    "tuft3": (72, 56, 16, 16),
    "tuft4": (88, 56, 16, 16),
    "tuft5": (0, 64, 16, 16),
    "tuft6": (16, 64, 16, 16),
    "pad_side0": (0, 80, 96, 16),
    "pad_side1": (0, 96, 96, 16),
    "pad_side2": (0, 112, 96, 16),
    "branch": (96, 72, 16, 56),
    "bark": (112, 0, 16, 128),
}
# Names the mesh asks for that reuse another painted rect.
COMPACT128_ALIASES = {"tuft7": "tuft0", "pad_side3": "pad_side0"}


def _luma(palette, key):
    r, g, b = (float(c) for c in palette[key][:3])
    return 0.299 * r + 0.587 * g + 0.114 * b


def _shade_keys(hist, palette):
    return sorted(hist, key=lambda k: (_luma(palette, k), k))


def _finish_ranked(canvas, hist, keys, outline_dir=(0, 1)):
    """Canvas.finish with keys in shade order and ramp-relative outline keys."""
    m = ~np.isnan(canvas.S)
    rank = np.zeros((canvas.h, canvas.w), np.int32)
    cnt = np.array([hist[k] for k in keys], float)
    vals = canvas.S[m]
    order = np.argsort(vals)
    out = np.empty(vals.size, np.int32)
    cut = np.round(np.cumsum(cnt) / cnt.sum() * vals.size).astype(int)
    s0 = 0
    for r, c in enumerate(cut):
        out[order[s0:c]] = r
        s0 = c
    rank[m] = out
    ox, _oy = outline_dir
    shifts = ((0, 1), (int(np.sign(ox)), 0)) if ox else ((0, 1),)
    top = len(keys) - 1
    for dx, dy in shifts:
        nb = np.zeros_like(canvas.L)
        ys = slice(0, canvas.h - dy) if dy > 0 else slice(None)
        yd = slice(dy, canvas.h) if dy > 0 else slice(None)
        xs = slice(0, canvas.w - dx) if dx > 0 else (slice(-dx, canvas.w) if dx < 0 else slice(None))
        xd = slice(dx, canvas.w) if dx > 0 else (slice(0, canvas.w + dx) if dx < 0 else slice(None))
        nb[ys, xs] = canvas.L[yd, xd]
        edge = m & (nb != canvas.L) & (canvas.Q > 0.55)
        empty = edge & (nb == 0)
        rank[edge] = np.minimum(rank[edge], min(1 if dy else 2, top))
        rank[empty & (dy > 0)] = 0
    idx = np.zeros((canvas.h, canvas.w), np.int32)
    idx[m] = np.array(keys, np.int32)[rank[m]]
    return idx


def _speckle(idx, speckle, rng):
    if not speckle:
        return idx
    m = idx > 0
    roll = rng.random(idx.shape)
    lo = 0.0
    for key, frac in speckle.items():
        hit = m & (roll >= lo) & (roll < lo + float(frac))
        idx[hit] = int(key)
        lo += float(frac)
    return idx


def paint_compact128(spec, palette):
    width, height = spec.get("size", [128, 128])
    if (width, height) != (128, 128):
        raise ValueError("layout compact128 is a 128x128 atlas")
    rng = np.random.default_rng(int(spec.get("seed", DEFAULT_SEED)))
    top_hist = _as_int_keyed(spec.get("top_hist")) or DEFAULT_TOP_HIST
    side_hist = _as_int_keyed(spec.get("side_hist")) or DEFAULT_SIDE_HIST
    sprite_hist = _as_int_keyed(spec.get("sprite_hist")) or DEFAULT_SPRITE_HIST
    bark_hist = _as_int_keyed(spec.get("bark_hist")) or DEFAULT_BARK_HIST
    speckle = {int(k): float(v) for k, v in (spec.get("speckle") or {}).items()}
    top_keys = _shade_keys(top_hist, palette)
    side_keys = _shade_keys(side_hist, palette)
    sprite_keys = _shade_keys(sprite_hist, palette)

    indices = np.zeros((height, width), np.int32)
    tiles = {}

    def put(name, arr):
        x, y, w, h = COMPACT128_TILES[name]
        assert arr.shape == (h, w), (name, arr.shape)
        indices[y : y + h, x : x + w] = arr
        tiles[name] = [int(x), int(y), int(w), int(h)]

    def pad_top(w, h):
        c = Canvas(h, w, rng)
        n = max(4, int(w * h / 150))
        pts = []
        while len(pts) < n:
            u, v = rng.uniform(-1, 1, 2)
            if u * u + v * v < 0.75:
                pts.append((u, v))
        pts.sort(key=lambda p: p[1])
        for u, v in pts:
            rad = rng.uniform(0.2, 0.27) * w * (1 - 0.25 * (u * u + v * v))
            c.lump((u * 0.62 + 1) * w / 2, (v * 0.62 + 1) * h / 2, rad, (0.4, 0.9), base=0.0)
        return _speckle(_finish_ranked(c, top_hist, top_keys, (1, 1)), speckle, rng)

    def pad_side(w, h):
        c = Canvas(h, w, rng)
        for cyf, rlo, rhi, base in ((0.38, 4.5, 6.0, 0.12), (0.62, 4.0, 5.5, 0.0), (0.78, 3.0, 4.6, -0.12)):
            nx = int(w / (rlo + rhi) * 1.25)
            xs = np.linspace(0.05, 0.95, nx) * w + rng.uniform(-2, 2, nx)
            for x in xs:
                e = np.sin(np.pi * np.clip(x / w, 0, 1)) ** 0.4
                rad = rng.uniform(rlo, rhi) * (0.65 + 0.35 * e)
                c.lump(x, h * cyf + rng.uniform(-0.8, 0.8), rad, (0, 1), base=base, squash=0.7)
        return _speckle(_finish_ranked(c, side_hist, side_keys, (0, 1)), speckle, rng)

    for name, (_x, _y, w, h) in COMPACT128_TILES.items():
        if name.startswith("pad_top"):
            put(name, pad_top(w, h))
        elif name.startswith("pad_side"):
            put(name, pad_side(w, h))
        elif name.startswith("tuft"):
            c = Canvas(h, w, rng)
            for _ in range(3):
                c.lump(rng.uniform(5, 11), rng.uniform(6, 10), rng.uniform(3.5, 5), (0, 1))
            put(name, _speckle(_finish_ranked(c, sprite_hist, sprite_keys, (0, 1)), speckle, rng))

    # Bark: vertical streaks, matched in the given key order (default shade order).
    _x, _y, bw, bh = COMPACT128_TILES["bark"]
    noise = (
        np.repeat(rng.random((1, bw)), bh, 0) * 0.7
        + np.repeat(rng.random((8, bw)), bh // 8, 0) * 0.15
        + rng.random((bh, bw)) * 0.15
    )
    order = np.argsort(noise, axis=None)
    out = np.empty(noise.size, np.int32)
    keys = [int(k) for k in spec.get("bark_key_order") or _shade_keys(bark_hist, palette)]
    cnt = np.array([bark_hist[k] for k in keys], float)
    cut = np.round(np.cumsum(cnt) / cnt.sum() * noise.size).astype(int)
    s0 = 0
    for key, c in zip(keys, cut):
        out[order[s0:c]] = key
        s0 = c
    put("bark", out.reshape(bh, bw))
    # Branch wood: a short ramp of the given keys (default the pine's browns 34-37).
    bkeys = [int(k) for k in spec.get("branch_keys") or [34, 35, 36, 37]]
    _x, _y, rw, rh = COMPACT128_TILES["branch"]
    ramp = (np.repeat(rng.random((1, rw)), rh, 0) * (len(bkeys) - 0.8) + rng.random((rh, rw)) * 1.0).astype(int)
    put("branch", np.array(bkeys, np.int32)[np.clip(ramp, 0, len(bkeys) - 1)])

    for alias, src in COMPACT128_ALIASES.items():
        tiles[alias] = list(tiles[src])

    rgba = np.zeros((height, width, 4), np.uint8)
    opaque = indices > 0
    rgba[opaque, :3] = palette[indices[opaque]]
    rgba[opaque, 3] = 255
    return rgba, tiles, indices


def write_atlas(params, palette, png_path, json_path=None):
    painter = ((params.get("atlas") or {}).get("painter") or "conifer").lower()
    if painter == "tree128":
        from .tree_atlas import paint as paint_tree128

        rgba, tiles, _indices = paint_tree128(params, palette)
    elif painter == "conifer":
        rgba, tiles, _indices = paint(params, palette)
    else:
        raise ValueError(f"unknown atlas painter {painter!r} (conifer or tree128)")
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
