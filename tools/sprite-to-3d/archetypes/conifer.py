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
    # Each clump's limb runs to the clump centre plus limb_reach x its larger
    # radius. 0 ends the limb under the pad (pads sit on the branch ends).
    limb_reach=0.6,
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
    # Optional "tilt": [dz/dx, dz/dy] in world axes tips the whole pad (top,
    # skirt, under, crown, tufts) as one plane, e.g. outward droop or a pad
    # tilted at a branch end. Without it the pad keeps the small random tilt
    # of the top card only, so clumps without "tilt" build exactly as before.
    tilt = clump.get("tilt")
    if tilt:
        tlx = float(tilt[0]) * ca + float(tilt[1]) * sa
        tly = -float(tilt[0]) * sa + float(tilt[1]) * ca

        def zf(u, v, z):
            return z + tlx * u + tly * v

        corners = [loc(su * a * ts, sv * b * ts, zf(su * a * ts, sv * b * ts, zt)) for su, sv in ((-1, -1), (1, -1), (1, 1), (-1, 1))]
    else:

        def zf(u, v, z):
            return z

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
            loc(xa * ka, ya * ka, zf(xa * ka, ya * ka, z0 + dz)),
            loc(xb * kb, yb * kb, zf(xb * kb, yb * kb, z0 + dz)),
            loc(xb * jb, yb * jb, zf(xb * jb, yb * jb, z1 + dz)),
            loc(xa * ja, ya * ja, zf(xa * ja, ya * ja, z1 + dz)),
        ]
        add_card(mesh, side_tile, corners, [(ucur, 1), (ucur + uw, 1), (ucur + uw, 0), (ucur, 0)], "pad_side")
        ucur += uw

    under = tiles["pad_top_S%d" % ((index + 2) % 4)]
    su = quality["skirt_w"] * 0.98
    add_card(
        mesh,
        under,
        [loc(au * a * su, bv * b * su, zf(au * a * su, bv * b * su, z0 + 0.6)) for au, bv in ((-1, -1), (1, -1), (1, 1), (-1, 1))],
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
        [loc(ox + su_ * a * s2, oy + sv * b * s2, zf(ox + su_ * a * s2, oy + sv * b * s2, zc)) for su_, sv in ((-1, -1), (1, -1), (1, 1), (-1, 1))],
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
        base = loc(cx, cy, zf(cx, cy, zt - rng.uniform(0.5, 2.5)))

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


DEFAULT_LUMP_PROFILE = ((0.0, 0.0), (0.12, 0.7), (0.42, 1.0), (0.78, 0.78), (1.0, 0.0))


def add_lump(mesh, tiles, clump, index, rng, spec):
    """Opt-in rounded foliage lump: a closed, faceted body instead of cards.

    Used when a clump carries "lump" (true or a dict); clumps without it go
    through add_foliage unchanged. The body is an N-sided (default 7) lathe
    around the clump centre, from z - hb to z + ht, through the "profile" rows
    [[t, k], ...] (t 0..1 up the height, k times the clump's a/b ellipse; k = 0
    closes the end in a single apex). Each lump gets a random yaw, a random
    per-column radius jitter and a random xy offset (fraction of a/b), so
    stacked lumps overlap into irregular bulging lobes. Sides sample the
    opaque core of the pad_side tiles (a different tile per band); bands with
    t >= "cap_from" (off by default) use a pad_top tile projected from above.
    """
    spec = spec if isinstance(spec, dict) else {}
    sides = int(spec.get("sides", 7))
    profile = [(float(t), float(k)) for t, k in spec.get("profile", DEFAULT_LUMP_PROFILE)]
    jitter = float(spec.get("jitter", 0.12))
    off = float(spec.get("offset", 0.08))
    cap_from = float(spec.get("cap_from", 2.0))
    v_core = spec.get("v_core", (0.2, 0.8))
    tpu = float(spec.get("side_tpu", 1.0))
    yaw = rng.uniform(0, 2 * math.pi)
    ox = clump["x"] + rng.uniform(-off, off) * clump["a"]
    oy = clump["y"] + rng.uniform(-off, off) * clump["b"]
    kj = [1.0 + rng.uniform(-jitter, jitter) for _ in range(sides)]
    tip = spec.get("tip", (0.0, 0.0))
    ca, sa = math.cos(clump["th"]), math.sin(clump["th"])
    a, b = clump["a"], clump["b"]
    z0 = clump["z"] - clump["hb"]
    z1 = clump["z"] + clump["ht"]
    top_tile = tiles[("pad_top_L%d" if max(a, b) >= 9 else "pad_top_S%d") % (index % 4)]
    kmax = max(k for _t, k in profile)
    perimeter = 2 * math.pi * max(a, b) * kmax
    side_w = tiles["pad_side0"][2]
    uspan = min(1.0, perimeter * tpu / side_w)
    u0 = rng.uniform(0, 1.0 - uspan)

    def point(t, k, j):
        z = z0 + t * (z1 - z0)
        if k <= 0:
            dx, dy = (float(tip[0]), float(tip[1])) if t >= 1.0 else (0.0, 0.0)
            return (ox + dx, oy + dy, z)
        ang = yaw + 2 * math.pi * j / sides
        u = k * kj[j % sides] * a * math.cos(ang)
        v = k * kj[j % sides] * b * math.sin(ang)
        return (ox + u * ca - v * sa, oy + u * sa + v * ca, z)

    rmax = max(a, b) * kmax * (1 + jitter)

    def cap_uv(p):
        return tile_uv(top_tile, 0.5 + 0.42 * (p[0] - ox) / rmax, 0.5 + 0.42 * (p[1] - oy) / rmax)

    # Each band between two profile rows gets its own pad_side tile and its own
    # stretch of the tile's opaque core, at side_tpu texels per sim unit along the
    # band's slant (squeezed only if a band is taller than the core).
    for band, ((t0, k0), (t1, k1)) in enumerate(zip(profile, profile[1:])):
        cap = t0 >= cap_from
        kind = "pad_top" if cap else "pad_side"
        side_tile = tiles["pad_side%d" % ((index + band) % 4)]
        core = (v_core[1] - v_core[0]) * side_tile[3]
        slant = math.hypot((t1 - t0) * (z1 - z0), (k1 - k0) * 0.5 * (a + b))
        span = min(core, slant * tpu)
        vstart = v_core[0] * side_tile[3] + rng.uniform(0, core - span)
        rows = []
        for t, k in ((t0, k0), (t1, k1)):
            vv = (vstart + (span if t == t0 else 0.0)) / side_tile[3]
            row = []
            for j in range(sides + 1):
                p = point(t, k, j)
                uv = cap_uv(p) if cap else tile_uv(side_tile, u0 + uspan * j / sides, vv)
                row.append(mesh.v(p, uv))
            rows.append(row)
        r0, r1 = rows
        for j in range(sides):
            if k0 <= 0:
                mesh.f((r0[j], r1[j + 1], r1[j]), kind)
            elif k1 <= 0:
                mesh.f((r0[j], r0[j + 1], r1[j]), kind)
            else:
                mesh.f((r0[j], r0[j + 1], r1[j + 1], r1[j]), kind)


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


def add_leader(mesh, tiles, leader):
    """Optional single-leader point: crossed triangle cards from a base width to a tip.

    leader: {"base": [x, y, z], "tip": [x, y, z], "r": half width at the base,
    "cards": 3, "tile": "pad_side0", "uv": [u0, v0, u1, v1], "phase": 0.3}.
    The uv rect should sit in an opaque band of the tile so the triangle edge,
    not the alpha, shapes the point. Not used by the Corsican Pine.
    """
    tile = tiles[leader.get("tile", "pad_side0")]
    base = tuple(float(c) for c in leader["base"])
    tip = tuple(float(c) for c in leader["tip"])
    r = float(leader["r"])
    n = int(leader.get("cards", 3))
    u0, v0, u1, v1 = (float(c) for c in leader.get("uv", [0.1, 0.25, 0.4, 0.75]))
    phase = float(leader.get("phase", 0.3))
    for k in range(n):
        ang = phase + math.pi * k / n
        dx, dy = r * math.cos(ang), r * math.sin(ang)
        add_card(
            mesh,
            tile,
            [(base[0] - dx, base[1] - dy, base[2]), (base[0] + dx, base[1] + dy, base[2]), tip],
            [(u0, v1), (u1, v1), ((u0 + u1) / 2, v0)],
            leader.get("kind", "tuft"),
        )


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
            length = min(max(distance + q["limb_reach"] * max(clump["a"], clump["b"]), 6.0), 24.0)
            z0 = clump["z"] - 3.0
            r0 = float(root[2]) * 0.8
            p0 = (rx + ux * r0, ry + uy * r0, z0)
            p1 = (rx + ux * length * 0.5, ry + uy * length * 0.5, z0 + 1.8)
            p2 = (rx + ux * length, ry + uy * length, z0 + 0.6)
        else:
            distance = math.hypot(clump["x"], clump["y"])
            angle = math.atan2(clump["y"], clump["x"]) if distance > 2 else clump["th"]
            ux, uy = math.cos(angle), math.sin(angle)
            length = min(max(distance + q["limb_reach"] * max(clump["a"], clump["b"]), 6.0), 24.0)
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
        if clump.get("lump"):
            add_lump(mesh, tiles, clump, i, rng, clump["lump"])
        else:
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
    for leader in params.get("leaders", []):
        add_leader(mesh, tiles, leader)
    return mesh
