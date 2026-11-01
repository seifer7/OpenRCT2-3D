# Copyright (c) 2026 OpenRCT2 developers
# Licensed under the GNU General Public License version 3.

"""Compact 128x128 pixel-art foliage atlas for the conifer mesh builder.

Same idea as conifer_atlas.py (lumps painted as shades, histogram-matched to
the sprite's own palette-index counts, coloured with the caller's palette,
alpha 0 or 255), packed into the 128² default instead of the pine's 256².
The tile names are the ones archetypes/conifer.py reads, so any conifer
params can select this painter with  "atlas": {"painter": "tree128", ...}.

Differences from the pine painter, all driven by params:
  * edge/rim indices come from the histogram (darkest keys), not the pine's
    hard-coded 142-144, so a cypress (82-93) or a cedar (70-80) palette works;
  * "coverage" < 1 drops lumps to leave holes for a sparse, lacy crown;
  * "lump_scale" changes the needle-lump size (small = fine yew needles);
  * "bark_key_order" / "branch_keys" set the wood ramps.

Layout (x, y, w, h):
  pad_top_L0..3  32x32 at (0|32, 0|32)        large top cards
  pad_top_S0..3  16x16 at (64|80, 0|16)       small tops, crowns, undersides
  tuft0..7       16x16 at (64|80, 32+16k)     rim tufts
  pad_side0..3   64x16 at (0, 64+16i)         skirt cards
  branch         8x64  at (104, 0)
  bark           16x128 at (112, 0)           1 texel per sim unit up a trunk of up to 128 sim
"""

from __future__ import annotations

import numpy as np

from .conifer_atlas import Canvas, bark_counts

SIZE = (128, 128)


def _hist(spec, key, fallback=None):
    raw = spec.get(key)
    if raw is None:
        return fallback
    return {int(k): int(v) for k, v in raw.items()}


class TreeCanvas(Canvas):
    """Canvas whose rim colours are the darkest keys of the target histogram."""

    def finish_keys(self, hist, outline_dir=(0, 1)):
        m = ~np.isnan(self.S)
        idx = np.zeros((self.h, self.w), np.int32)
        if not m.any():
            return idx
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
        dark0 = keys[0]
        dark1 = keys[min(1, len(keys) - 1)]
        dark2 = keys[min(2, len(keys) - 1)]
        ox, _oy = outline_dir
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
            idx[edge] = np.minimum(idx[edge], dark1 if dy else dark2)
            idx[empty & (dy > 0)] = dark0
        return idx


def paint(params, palette):
    """Return (rgba uint8 array, tiles dict, index array)."""
    spec = params.get("atlas") or {}
    width, height = spec.get("size", list(SIZE))
    if (width, height) != SIZE:
        raise ValueError("tree128 painter lays out a 128x128 atlas only")
    rng = np.random.default_rng(int(spec.get("seed", 2611)))
    top_hist = _hist(spec, "top_hist")
    side_hist = _hist(spec, "side_hist", top_hist)
    tuft_hist = _hist(spec, "sprite_hist", top_hist)
    bark_hist = _hist(spec, "bark_hist")
    if not top_hist or not bark_hist:
        raise ValueError("tree128 painter needs atlas.top_hist and atlas.bark_hist counted on the sprite")
    coverage = float(spec.get("coverage", 1.0))
    lump_scale = float(spec.get("lump_scale", 1.0))
    light = tuple(spec.get("light", (0.4, 0.9)))

    indices = np.zeros((height, width), np.int32)
    tiles = {}

    def tile(name, x, y, w, h, arr):
        indices[y : y + h, x : x + w] = arr
        tiles[name] = [int(x), int(y), int(w), int(h)]

    def keep():
        return coverage >= 1.0 or rng.random() < coverage

    def pad_top(w, h):
        c = TreeCanvas(h, w, rng)
        n = max(5, int(w * h / (110 * lump_scale * lump_scale)))
        if coverage < 1.0:
            n = int(n * 1.6)
        pts = []
        while len(pts) < n:
            u, v = rng.uniform(-1, 1, 2)
            if u * u + v * v < 0.75:
                pts.append((u, v))
        pts.sort(key=lambda p: p[1])
        for u, v in pts:
            rad = rng.uniform(0.15, 0.22) * w * lump_scale * (1 - 0.25 * (u * u + v * v))
            if keep():
                c.lump((u * 0.66 + 1) * w / 2, (v * 0.66 + 1) * h / 2, rad, light, base=0.0)
        return c.finish_keys(top_hist, (1, 1))

    def pad_side(w, h):
        c = TreeCanvas(h, w, rng)
        for cyf, rlo, rhi, base in ((0.38, 4.5, 6.0, 0.12), (0.62, 4.0, 5.5, 0.0), (0.78, 3.0, 4.6, -0.12)):
            rlo *= lump_scale
            rhi *= lump_scale
            nx = int(w / (rlo + rhi) * 1.25)
            xs = np.linspace(0.05, 0.95, nx) * w + rng.uniform(-2, 2, nx)
            for x in xs:
                e = np.sin(np.pi * np.clip(x / w, 0, 1)) ** 0.4
                rad = rng.uniform(rlo, rhi) * (0.65 + 0.35 * e)
                if keep():
                    c.lump(x, h * cyf + rng.uniform(-0.8, 0.8), rad, (0, 1), base=base, squash=0.7)
        return c.finish_keys(side_hist, (0, 1))

    for i in range(4):
        tile(f"pad_top_L{i}", 32 * (i % 2), 32 * (i // 2), 32, 32, pad_top(32, 32))
    for i in range(4):
        tile(f"pad_top_S{i}", 64 + 16 * (i % 2), 16 * (i // 2), 16, 16, pad_top(16, 16))
    for i in range(8):
        c = TreeCanvas(16, 16, rng)
        for _ in range(3):
            c.lump(rng.uniform(5, 11), rng.uniform(6, 10), rng.uniform(3.5, 5) * lump_scale, (0, 1))
        tile(f"tuft{i}", 64 + 16 * (i % 2), 32 + 16 * (i // 2), 16, 16, c.finish_keys(tuft_hist, (0, 1)))
    for i in range(4):
        tile(f"pad_side{i}", 0, 64 + 16 * i, 64, 16, pad_side(64, 16))

    # Bark: vertical streaks, histogram matched; key order is the streak order.
    bh, bw = 128, 16
    noise = (
        np.repeat(rng.random((1, bw)), bh, 0) * 0.7
        + np.repeat(rng.random((8, bw)), bh // 8, 0) * 0.15
        + rng.random((bh, bw)) * 0.15
    )
    order = np.argsort(noise, axis=None)
    out = np.empty(noise.size, np.int32)
    keys = [int(k) for k in spec.get("bark_key_order") or sorted(bark_hist)]
    cnt = bark_counts(bark_hist, keys, "tree128")
    cut = np.round(np.cumsum(cnt) / cnt.sum() * noise.size).astype(int)
    s0 = 0
    for key, c in zip(keys, cut):
        out[order[s0:c]] = key
        s0 = c
    tile("bark", 112, 0, bw, bh, out.reshape(bh, bw))
    branch_keys = [int(k) for k in spec.get("branch_keys") or keys[:4]]
    ramp = (np.repeat(rng.random((1, 8)), 64, 0) * (len(branch_keys) - 0.8) + rng.random((64, 8)) * 1.0).astype(int)
    tile("branch", 104, 0, 8, 64, np.array(branch_keys)[np.clip(ramp, 0, len(branch_keys) - 1)])

    rgba = np.zeros((height, width, 4), np.uint8)
    opaque = indices > 0
    rgba[opaque, :3] = palette[indices[opaque]]
    rgba[opaque, 3] = 255
    return rgba, tiles, indices
