# Copyright (c) 2026 OpenRCT2 developers
# Licensed under the GNU General Public License version 3.

"""Prop archetype: solid, mostly opaque small scenery built from shaped parts.

Barrels, cannons, crates, posts. These are not foliage, so the mesh is real
closed geometry rather than cut-out cards: lathes (turned profiles such as a
barrel or a gun barrel), beams (tapered boxes such as a carriage cheek or a
trail) and discs (flat cut-out cards, used only where the object really is a
thin perforated plate, such as a spoked wheel). Pixel detail stays on the atlas
(prop_atlas.py), painted from the sprite's own palette indices.

Positions are RCT sim units, origin at the tile (or quadrant) centre, Z up,
Z = 0 at the element base. UVs are atlas texel coordinates with the origin at
the top-left of the atlas, the same convention as the conifer archetype.

params["parts"] is a list of part dicts, built in order:

  {"kind": "lathe", "tile": "staves", "origin": [x, y, z], "axis": [dx, dy, dz],
   "profile": [[t, r], ...], "sides": 16, "phase": 0.0,
   "seg_tiles": {"3": "bore"},           # optional per-segment tile override
   "cap_start": "lid" | null, "cap_end": "lid" | null}

  {"kind": "beam", "tile": "wood", "p0": [...], "p1": [...],
   "size0": [half_width, half_height], "size1": [...], "up": [0, 0, 1],
   "ends": true}

  {"kind": "disc", "tile": "wheel", "centre": [...], "axis": [...],
   "radius": 5.0, "sides": 12, "phase": 0.0}

Every part may carry "mirror": "y" (or "x") to add a mirrored copy, and
"offset": [dx, dy, dz] which params["offset"] also applies to the whole prop.
"""

from __future__ import annotations

import math

from .conifer import Mesh, triangle_count  # noqa: F401  (re-exported for callers)

UV_INSET = 0.25


def _norm(v):
    length = math.sqrt(sum(c * c for c in v)) or 1.0
    return tuple(c / length for c in v)


def _cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def _add(*vs):
    return tuple(sum(c) for c in zip(*vs))


def _mul(v, k):
    return tuple(c * k for c in v)


def _basis(axis):
    """Unit axis plus two perpendiculars. b is horizontal when it can be."""
    a = _norm(axis)
    ref = (0.0, 0.0, 1.0) if abs(a[2]) < 0.9 else (1.0, 0.0, 0.0)
    b = _norm(_cross(ref, a))
    c = _cross(a, b)
    return a, b, c


def _tile_uv(tile, u, v):
    """u, v in 0..1 inside the tile, inset so nearest never samples a neighbour."""
    x, y, w, h = tile
    return (x + UV_INSET + u * (w - 2 * UV_INSET), y + UV_INSET + v * (h - 2 * UV_INSET))


def add_lathe(mesh, tiles, part, offset):
    tile = tiles[part["tile"]]
    a, b, c = _basis(part.get("axis", (0, 0, 1)))
    origin = _add(part.get("origin", (0, 0, 0)), offset)
    profile = [(float(t), float(r)) for t, r in part["profile"]]
    sides = int(part.get("sides", 12))
    phase = float(part.get("phase", 0.0))
    seg_tiles = {int(k): v for k, v in (part.get("seg_tiles") or {}).items()}
    # v runs along the profile by arc length, segments with their own tile excluded.
    lengths = [math.hypot(t1 - t0, r1 - r0) for (t0, r0), (t1, r1) in zip(profile, profile[1:])]
    main_total = sum(l for k, l in enumerate(lengths) if k not in seg_tiles) or 1.0

    def point(t, r, j):
        ang = phase + 2 * math.pi * j / sides
        return _add(origin, _mul(a, t), _mul(b, r * math.cos(ang)), _mul(c, r * math.sin(ang)))

    acc = 0.0
    for k, ((t0, r0), (t1, r1)) in enumerate(zip(profile, profile[1:])):
        if k in seg_tiles:
            seg_tile, va, vb = tiles[seg_tiles[k]], 0.0, 1.0
        else:
            seg_tile = tile
            va, vb = acc / main_total, (acc + lengths[k]) / main_total
            acc += lengths[k]
        if lengths[k] < 1e-9:
            continue
        row0 = [mesh.v(point(t0, r0, j), _tile_uv(seg_tile, j / sides, va)) for j in range(sides + 1)]
        row1 = [mesh.v(point(t1, r1, j), _tile_uv(seg_tile, j / sides, vb)) for j in range(sides + 1)]
        for j in range(sides):
            if r0 < 1e-6:
                mesh.f((row0[j], row1[j + 1], row1[j]), part["tile"])
            elif r1 < 1e-6:
                mesh.f((row0[j], row0[j + 1], row1[j]), part["tile"])
            else:
                mesh.f((row0[j], row0[j + 1], row1[j + 1], row1[j]), part["tile"])
    for key, (t, r), sign in (("cap_start", profile[0], -1), ("cap_end", profile[-1], 1)):
        name = part.get(key)
        if not name or r < 1e-6:
            continue
        cap = tiles[name]
        centre = mesh.v(_add(origin, _mul(a, t)), _tile_uv(cap, 0.5, 0.5))
        ring = []
        for j in range(sides):
            ang = phase + 2 * math.pi * j / sides
            ring.append(mesh.v(point(t, r, j), _tile_uv(cap, 0.5 + 0.5 * math.cos(ang), 0.5 - 0.5 * math.sin(ang))))
        for j in range(sides):
            jn = (j + 1) % sides
            mesh.f((centre, ring[j], ring[jn]) if sign > 0 else (centre, ring[jn], ring[j]), name)


def add_beam(mesh, tiles, part, offset):
    """Tapered box from p0 to p1. Side faces map 1 texel per sim unit from the
    tile's top-left, clamped to the tile; end faces use the tile's corner."""
    tile = tiles[part["tile"]]
    p0 = _add(part["p0"], offset)
    p1 = _add(part["p1"], offset)
    d = _norm(tuple(q - p for p, q in zip(p0, p1)))
    up_hint = part.get("up", (0, 0, 1))
    side = _cross(d, up_hint)
    if math.sqrt(sum(s * s for s in side)) < 1e-6:
        side = _cross(d, (1, 0, 0))
    side = _norm(side)
    up = _cross(side, d)
    w0, h0 = part["size0"]
    w1, h1 = part.get("size1", part["size0"])
    length = math.sqrt(sum((q - p) ** 2 for p, q in zip(p0, p1)))
    corners = ((-1, -1), (1, -1), (1, 1), (-1, 1))

    def ring(p, w, h):
        return [_add(p, _mul(side, sx * w), _mul(up, sy * h)) for sx, sy in corners]

    r0 = ring(p0, w0, h0)
    r1 = ring(p1, w1, h1)
    x, y, tw, th = tile
    for i in range(4):
        j = (i + 1) % 4
        width = 2 * (h0 + h1) / 2 if i % 2 else 2 * (w0 + w1) / 2
        fu = min(1.0, length / max(tw, 1))
        fv = min(1.0, width / max(th, 1))
        quad = (r0[i], r1[i], r1[j], r0[j])
        uvs = ((0, 0), (fu, 0), (fu, fv), (0, fv))
        mesh.f([mesh.v(p, _tile_uv(tile, *uv)) for p, uv in zip(quad, uvs)], part["tile"])
    if part.get("ends", True):
        # Corners run counter-clockwise in the (side, up) plane, so a ring's
        # natural order faces -d: outward at p0. The p1 cap is reversed so it
        # faces +d. Normals come from the winding, so these must face out.
        for r, rev in ((r0, False), (r1, True)):
            fu = min(1.0, 2 * max(w0, w1) / max(tw, 1))
            fv = min(1.0, 2 * max(h0, h1) / max(th, 1))
            uvs = ((0, fv), (fu, fv), (fu, 0), (0, 0))
            idx = [mesh.v(p, _tile_uv(tile, *uv)) for p, uv in zip(r, uvs)]
            mesh.f(tuple(reversed(idx)) if rev else tuple(idx), part["tile"])


def add_disc(mesh, tiles, part, offset):
    tile = tiles[part["tile"]]
    a, b, c = _basis(part["axis"])
    centre = _add(part["centre"], offset)
    radius = float(part["radius"])
    sides = int(part.get("sides", 12))
    phase = float(part.get("phase", 0.0))
    mid = mesh.v(centre, _tile_uv(tile, 0.5, 0.5))
    ring = []
    for j in range(sides):
        ang = phase + 2 * math.pi * j / sides
        p = _add(centre, _mul(b, radius * math.cos(ang)), _mul(c, radius * math.sin(ang)))
        ring.append(mesh.v(p, _tile_uv(tile, 0.5 + 0.5 * math.cos(ang), 0.5 - 0.5 * math.sin(ang))))
    for j in range(sides):
        mesh.f((mid, ring[j], ring[(j + 1) % sides]), part["tile"])


BUILDERS = {"lathe": add_lathe, "beam": add_beam, "disc": add_disc}


def _mirrored(part, axis):
    k = {"x": 0, "y": 1}[axis]
    out = dict(part)
    for key in ("origin", "p0", "p1", "centre"):
        if key in part:
            v = list(part[key])
            v[k] = -v[k]
            out[key] = v
    for key in ("axis", "up"):
        if key in part:
            v = list(part[key])
            v[k] = -v[k]
            out[key] = v
    out.pop("mirror", None)
    return out


def expand_parts(params):
    parts = []
    for part in params["parts"]:
        parts.append(part)
        if part.get("mirror"):
            parts.append(_mirrored(part, part["mirror"]))
    return parts


def build(params, tiles):
    mesh = Mesh()
    base = tuple(params.get("offset", (0.0, 0.0, 0.0)))
    for part in expand_parts(params):
        offset = _add(base, part.get("offset", (0.0, 0.0, 0.0)))
        BUILDERS[part["kind"]](mesh, tiles, part, offset)
    return mesh
