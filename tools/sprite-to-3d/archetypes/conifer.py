# Copyright (c) 2026 OpenRCT2 developers
# Licensed under the GNU General Public License version 3.

"""Conifer archetype: flared trunk, drooping branch tiers, cut-out needle cards.

Positions are RCT sim units, origin at the tile centre, Z up, Z = 0 at the
element base. UVs are atlas texel coordinates with the origin at the top-left
of the atlas. The Corsican Pine params reproduce the approved 864-triangle mesh.
The numeric construction is the approved builder, with the trunk rings and the
card proportions read from the params dict instead of being hard-coded.
"""

from __future__ import annotations

import math
import random

# Approved Corsican Pine trunk, used when params omit trunk_rings.
DEFAULT_TRUNK_RINGS = (
    (-3, 4.4),
    (0, 3.9),
    (3, 3.2),
    (8, 2.8),
    (30, 2.6),
    (60, 2.3),
    (90, 1.8),
    (115, 1.3),
    (134, 0.5),
)

DEFAULT_QUALITY = dict(
    pad_scale=1.1,
    top_scale=1.2,
    top_k=0.35,
    skirt_lo=0.5,
    skirt_hi=1.5,
    skirt_w=0.86,
    ring=6,
    side_tpu=1.6,
    skirt_lean=1.2,
    crown_k=0.6,
    crown_dz=2.0,
    tufts=4,
    tuft_size=0.33,
    br_r0=0.75,
    br_r1=0.45,
    sub=2,
    twig_r=0.5,
    # Added for later objects; the defaults are the pine's construction.
    limbs=1,  # 0 hides the per-clump drooping branch and its twigs (dense columnar crowns)
    trunk_sides=8,
)


class Mesh:
    def __init__(self):
        self.V = []
        self.UV = []
        self.F = []
        self.K = []

    def v(self, p, uv):
        self.V.append(tuple(p))
        self.UV.append(tuple(uv))
        return len(self.V) - 1

    def f(self, idx, kind):
        self.F.append(tuple(idx))
        self.K.append(kind)


def trunk_rings(params):
    rings = params.get("trunk_rings")
    if not rings:
        return list(DEFAULT_TRUNK_RINGS)
    return [(float(z), float(r)) for z, r in rings]


def trunk_radius(z, rings):
    if z <= rings[0][0]:
        return rings[0][1]
    for (z0, r0), (z1, r1) in zip(rings, rings[1:]):
        if z <= z1:
            return r0 + (r1 - r0) * (z - z0) / (z1 - z0)
    return rings[-1][1]


def tile_uv(tile, u, v):
    x, y, w, h = tile
    return (x + u * w, y + v * h)


def add_trunk(mesh, tiles, rings, sides=8, v_denom=138.0):
    """v_denom is the approved pine's (tip - base + 1) UV span, 138, not 137."""
    bark = tiles["bark"]
    z0 = rings[0][0]
    rows = []
    for z, radius in rings:
        row = []
        for i in range(sides + 1):
            angle = 2 * math.pi * i / sides + 0.2
            row.append(
                mesh.v(
                    (radius * math.cos(angle), radius * math.sin(angle), z),
                    tile_uv(bark, i / sides, 1 - (z - z0) / v_denom),
                )
            )
        rows.append(row)
    for row0, row1 in zip(rows, rows[1:]):
        for i in range(sides):
            mesh.f((row0[i], row0[i + 1], row1[i + 1], row1[i]), "bark")


def frame(direction):
    dx, dy, dz = direction
    length = math.sqrt(dx * dx + dy * dy + dz * dz)
    d = (dx / length, dy / length, dz / length)
    side = (-d[1], d[0], 0.0)
    side_len = math.hypot(side[0], side[1]) or 1.0
    side = (side[0] / side_len, side[1] / side_len, 0.0)
    up = (
        d[1] * side[2] - d[2] * side[1],
        d[2] * side[0] - d[0] * side[2],
        d[0] * side[1] - d[1] * side[0],
    )
    return d, side, up


def add_limb(mesh, tiles, points, radii, sides):
    """Tapered prism along a polyline. A last radius of 0 closes to a point."""
    bark = tiles["branch"]
    rings = []
    for k, (point, radius) in enumerate(zip(points, radii)):
        direction = tuple(
            b - a
            for a, b in zip(points[max(k - 1, 0)], points[min(k + 1, len(points) - 1)])
        )
        _d, side, up = frame(direction)
        vv = k / (len(points) - 1)
        if radius <= 0:
            rings.append([mesh.v(point, tile_uv(bark, 0.5, vv))])
            continue
        ring = []
        for i in range(sides):
            angle = 2 * math.pi * i / sides + math.pi / 4
            q = tuple(
                point[j] + radius * (math.cos(angle) * side[j] + math.sin(angle) * up[j])
                for j in range(3)
            )
            ring.append(mesh.v(q, tile_uv(bark, (i + 0.5) / sides, vv)))
        rings.append(ring)
    for row0, row1 in zip(rings, rings[1:]):
        for i in range(sides):
            j = (i + 1) % sides
            if len(row1) == 1:
                mesh.f((row0[i], row0[j], row1[0]), "branch")
            else:
                mesh.f((row0[i], row0[j], row1[j], row1[i]), "branch")


def add_card(mesh, tile, corners, uvs, kind):
    mesh.f([mesh.v(c, tile_uv(tile, *uv)) for c, uv in zip(corners, uvs)], kind)


def add_foliage(mesh, tiles, clump, index, rng, quality):
    ca, sa = math.cos(clump["th"]), math.sin(clump["th"])
    a = clump["a"] * quality["pad_scale"]
    b = clump["b"] * quality["pad_scale"]

    def loc(u, v, z):
        return (clump["x"] + u * ca - v * sa, clump["y"] + u * sa + v * ca, z)

    zt = clump["z"] + quality["top_k"] * clump["ht"]
    big = max(a, b) >= 9
    tile = tiles[("pad_top_L%d" if big else "pad_top_S%d") % (index % 4)]
    tx, ty = rng.uniform(-0.06, 0.06), rng.uniform(-0.06, 0.06)
    ts = quality["top_scale"]
    corners = [
        loc(su * a * ts, sv * b * ts, zt + (su * tx * a + sv * ty * b))
        for su, sv in ((-1, -1), (1, -1), (1, 1), (-1, 1))
    ]
    add_card(mesh, tile, corners, [(0, 1), (1, 1), (1, 0), (0, 0)], "pad_top")

    z0 = clump["z"] - quality["skirt_lo"] * clump["hb"]
    z1 = zt + quality["skirt_hi"]
    n = int(quality["ring"])
    phase = rng.uniform(0, 2 * math.pi)
    angles = [phase + 2 * math.pi * (k + rng.uniform(-0.12, 0.12)) / n for k in range(n)]

    def rim(phi):
        ex, ey = math.cos(phi), math.sin(phi)
        radius = 1.0 / math.sqrt((ex / a) ** 2 + (ey / b) ** 2) * quality["skirt_w"]
        return radius * ex, radius * ey

    side_tile = tiles["pad_side%d" % (index % 4)]
    ucur = rng.uniform(0, 0.5)
    for k in range(n):
        (xa, ya), (xb, yb) = rim(angles[k]), rim(angles[(k + 1) % n] + (2 * math.pi if k == n - 1 else 0))
        uw = min(1.0, math.hypot(xb - xa, yb - ya) * quality["side_tpu"] / side_tile[2])
        if ucur + uw > 1:
            ucur = 0.0
        dz = rng.uniform(-1.5, 1.5)
        lean = quality["skirt_lean"]
        ka, kb = [1 - lean / max(math.hypot(xx, yy), 1) for xx, yy in ((xa, ya), (xb, yb))]
        ja, jb = [1 + lean / max(math.hypot(xx, yy), 1) for xx, yy in ((xa, ya), (xb, yb))]
        corners = [
            loc(xa * ka, ya * ka, z0 + dz),
            loc(xb * kb, yb * kb, z0 + dz),
            loc(xb * jb, yb * jb, z1 + dz),
            loc(xa * ja, ya * ja, z1 + dz),
        ]
        add_card(mesh, side_tile, corners, [(ucur, 1), (ucur + uw, 1), (ucur + uw, 0), (ucur, 0)], "pad_side")
        ucur += uw

    under = tiles["pad_top_S%d" % ((index + 2) % 4)]
    su = quality["skirt_w"] * 0.98
    add_card(
        mesh,
        under,
        [loc(au * a * su, bv * b * su, z0 + 0.6) for au, bv in ((-1, -1), (1, -1), (1, 1), (-1, 1))],
        [(0, 1), (1, 1), (1, 0), (0, 0)],
        "pad_under",
    )

    crown = tiles["pad_top_S%d" % ((index + 1) % 4)]
    s2 = quality["crown_k"]
    zc = zt + quality["crown_dz"]
    ox, oy = rng.uniform(-0.2, 0.2) * a, rng.uniform(-0.2, 0.2) * b
    add_card(
        mesh,
        crown,
        [loc(ox + su_ * a * s2, oy + sv * b * s2, zc) for su_, sv in ((-1, -1), (1, -1), (1, 1), (-1, 1))],
        [(0, 1), (1, 1), (1, 0), (0, 0)],
        "pad_top",
    )

    for k in range(int(quality["tufts"])):
        phi = rng.uniform(0, 2 * math.pi)
        ex, ey = math.cos(phi), math.sin(phi)
        radius = 1.0 / math.sqrt((ex / a) ** 2 + (ey / b) ** 2) * 0.85
        cx, cy = radius * ex, radius * ey
        size = min(7.0, max(3.0, quality["tuft_size"] * max(a, b)))
        tuft = tiles["tuft%d" % ((index * 3 + k) % 8)]
        tangent = (-ey, ex)
        elevation = rng.uniform(0.35, 0.8)
        up = (ex * math.cos(elevation), ey * math.cos(elevation), math.sin(elevation))
        base = loc(cx, cy, zt - rng.uniform(0.5, 2.5))

        def corner(au, bv, base=base, tangent=tangent, up=up, size=size):
            uu = au * size
            vv = bv * size
            dx = uu * tangent[0] + vv * up[0]
            dy = uu * tangent[1] + vv * up[1]
            return (base[0] + dx * ca - dy * sa, base[1] + dx * sa + dy * ca, base[2] + vv * up[2])

        add_card(
            mesh,
            tuft,
            [corner(-1, -0.6), corner(1, -0.6), corner(1, 1.0), corner(-1, 1.0)],
            [(0, 1), (1, 1), (1, 0), (0, 0)],
            "tuft",
        )


def add_stem(mesh, tiles, stem):
    """Optional extra trunk or limb along a polyline (multi-stem trees, forks).

    stem: {"points": [[x, y, z], ...], "radii": [...], "sides": 6, "tile": "bark",
    "texels_per_sim": 1.0}. V runs along the stem at texels_per_sim and restarts at
    the top of the tile when it would run off, so a long stem keeps about one texel
    per sim unit. Each segment has its own rings, which costs no triangles.
    Not used by the Corsican Pine.
    """
    tile = tiles[stem.get("tile", "bark")]
    points = [tuple(float(c) for c in p) for p in stem["points"]]
    radii = [float(r) for r in stem["radii"]]
    sides = int(stem.get("sides", 6))
    per_sim = float(stem.get("texels_per_sim", 1.0))
    tile_h = float(tile[3])
    kind = stem.get("kind", "bark")
    v_cursor = 0.0
    for k in range(len(points) - 1):
        a, b = points[k], points[k + 1]
        seg = math.dist(a, b)
        span = seg * per_sim
        if v_cursor + span > tile_h:
            v_cursor = 0.0
        span = min(span, tile_h)
        v0, v1 = v_cursor / tile_h, (v_cursor + span) / tile_h
        v_cursor += span
        rows = []
        for point, radius, vv, nb in ((a, radii[k], v0, k), (b, radii[k + 1], v1, k + 1)):
            # Ring frame from the averaged neighbouring segments so joints do not pinch.
            prev = points[max(nb - 1, 0)]
            nxt = points[min(nb + 1, len(points) - 1)]
            _d, side, up = frame(tuple(q - p for p, q in zip(prev, nxt)))
            if radius <= 0:
                rows.append([mesh.v(point, tile_uv(tile, 0.5, 1 - vv))])
                continue
            ring = []
            for i in range(sides + 1):
                angle = 2 * math.pi * i / sides + 0.2
                q = tuple(
                    point[j] + radius * (math.cos(angle) * side[j] + math.sin(angle) * up[j])
                    for j in range(3)
                )
                ring.append(mesh.v(q, tile_uv(tile, i / sides, 1 - vv)))
            rows.append(ring)
        row0, row1 = rows
        for i in range(sides):
            if len(row1) == 1:
                mesh.f((row0[i], row0[i + 1], row1[0]), kind)
            else:
                mesh.f((row0[i], row0[i + 1], row1[i + 1], row1[i]), kind)


def build(params, tiles, quality=None):
    rings = trunk_rings(params)
    q = dict(DEFAULT_QUALITY)
    q.update(params.get("quality") or {})
    if quality:
        q.update(quality)
    seed0 = int(params.get("foliage_seed", 500))
    mesh = Mesh()
    add_trunk(mesh, tiles, rings, sides=int(q["trunk_sides"]), v_denom=float(params.get("trunk_v_denom", 138)))
    for i, clump in enumerate(params["clumps"]):
        rng = random.Random(seed0 + i)
        # Optional "root": [x, y, r] hangs the clump's branch off a side stem
        # instead of the main trunk. The pine's clumps have no root.
        root = clump.get("root")
        if root:
            rx, ry = float(root[0]), float(root[1])
            distance = math.hypot(clump["x"] - rx, clump["y"] - ry)
            angle = math.atan2(clump["y"] - ry, clump["x"] - rx) if distance > 2 else clump["th"]
            ux, uy = math.cos(angle), math.sin(angle)
            length = min(max(distance + 0.6 * max(clump["a"], clump["b"]), 6.0), 24.0)
            z0 = clump["z"] - 3.0
            r0 = float(root[2]) * 0.8
            p0 = (rx + ux * r0, ry + uy * r0, z0)
            p1 = (rx + ux * length * 0.5, ry + uy * length * 0.5, z0 + 1.8)
            p2 = (rx + ux * length, ry + uy * length, z0 + 0.6)
        else:
            distance = math.hypot(clump["x"], clump["y"])
            angle = math.atan2(clump["y"], clump["x"]) if distance > 2 else clump["th"]
            ux, uy = math.cos(angle), math.sin(angle)
            length = min(max(distance + 0.6 * max(clump["a"], clump["b"]), 6.0), 24.0)
            z0 = clump["z"] - 3.0
            r0 = trunk_radius(z0, rings) * 0.8
            p0 = (ux * r0, uy * r0, z0)
            p1 = (ux * length * 0.5, uy * length * 0.5, z0 + 1.8)
            p2 = (ux * length, uy * length, z0 + 0.6)
        if q["limbs"]:
            add_limb(mesh, tiles, [p0, p1, p2], [q["br_r0"], q["br_r1"], 0], 4)
        for k in range(int(q["sub"]) if q["limbs"] else 0):
            sign = 1 if k == 0 else -1
            a2 = angle + sign * rng.uniform(0.6, 1.0)
            l2 = length * rng.uniform(0.35, 0.5)
            tip = (p1[0] + math.cos(a2) * l2, p1[1] + math.sin(a2) * l2, p1[2] + rng.uniform(-1.0, 0.8))
            add_limb(mesh, tiles, [p1, tip], [0.35, 0], 3)
        add_foliage(mesh, tiles, clump, i, rng, q)
    for twig in params.get("branches", []):
        ux, uy = math.cos(twig["ang"]), math.sin(twig["ang"])
        r0 = trunk_radius(twig["z"], rings) * 0.8
        length = twig["len"] + 2
        points = [
            (ux * r0, uy * r0, twig["z"]),
            (ux * length * 0.55, uy * length * 0.55, twig["z"] + 0.6 * twig["rise"] + 1.0),
            (ux * length, uy * length, twig["z"] + twig["rise"] - 0.5),
        ]
        add_limb(mesh, tiles, points, [q["twig_r"], q["twig_r"] * 0.6, 0], 4)
    for stem in params.get("stems", []):
        add_stem(mesh, tiles, stem)
    return mesh


def triangle_count(mesh):
    return sum(len(face) - 2 for face in mesh.F)
