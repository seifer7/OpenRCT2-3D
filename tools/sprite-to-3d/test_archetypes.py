# Copyright (c) 2026 OpenRCT2 developers
# Licensed under the GNU General Public License version 3.

"""Archetypes and painters, pure Python (no Blender, no RCT2 data).

Covers the prop archetype, the prop, compact128 and tree128 painters, the
conifer's add_stem and clump "root" option, a smoke build per archetype, and
outward winding on every closed part (normals come from the winding, so an
inward face renders dark or culled in the game).
"""

from __future__ import annotations

import copy
import json
import math
import os
import unittest

import numpy as np

import archetypes
from archetypes import conifer, conifer_atlas, prop, prop_atlas, tree_atlas

HERE = os.path.dirname(os.path.abspath(__file__))
PINE_PARAMS = os.path.join(HERE, "examples", "rct2.scenery_small.tcrp", "params.json")


def _palette():
    """Synthetic 256-entry palette: distinct colours, brightness rising with index."""
    i = np.arange(256)
    return np.stack([i, (i * 7) % 256, 255 - i], axis=1).astype(np.uint8)


def _pine_params():
    with open(PINE_PARAMS, encoding="utf-8") as fh:
        return json.load(fh)


def _newell(points):
    """Unnormalised polygon normal (Newell), robust for quads and n-gons."""
    n = np.zeros(3)
    for k in range(len(points)):
        a, b = points[k], points[(k + 1) % len(points)]
        n += ((a[1] - b[1]) * (a[2] + b[2]), (a[2] - b[2]) * (a[0] + b[0]), (a[0] - b[0]) * (a[1] + b[1]))
    return n


def _faces(mesh, start=0):
    V = np.asarray(mesh.V, float)
    return [V[list(face)] for face in mesh.F[start:]]


def _signed_volume(faces):
    total = 0.0
    for pts in faces:
        for k in range(1, len(pts) - 1):
            total += np.dot(pts[0], np.cross(pts[k], pts[k + 1])) / 6.0
    return total


def _assert_outward_convex(test, faces):
    """Closed convex part: positive volume and every face points away from the centre."""
    test.assertGreater(_signed_volume(faces), 0.0, "closed part winds inward (negative volume)")
    centre = np.mean(np.concatenate(faces), axis=0)
    for pts in faces:
        n = _newell(pts)
        if np.linalg.norm(n) < 1e-9:
            continue
        test.assertGreater(np.dot(n, pts.mean(axis=0) - centre), 0.0, f"inward face {pts.tolist()}")


def _assert_outward_from_axis(test, faces, axis_points):
    """Tube faces (open or not): each normal points away from the nearest point on the axis."""
    axis_points = [np.asarray(p, float) for p in axis_points]

    def nearest(c):
        best, best_d = None, math.inf
        for a, b in zip(axis_points, axis_points[1:]):
            ab = b - a
            t = np.clip(np.dot(c - a, ab) / np.dot(ab, ab), 0.0, 1.0)
            q = a + t * ab
            d = np.linalg.norm(c - q)
            if d < best_d:
                best, best_d = q, d
        return best

    for pts in faces:
        n = _newell(pts)
        if np.linalg.norm(n) < 1e-9:
            continue
        c = pts.mean(axis=0)
        test.assertGreater(np.dot(n, c - nearest(c)), 0.0, f"inward face {pts.tolist()}")


TILES = {"wood": [0, 0, 16, 16], "lid": [16, 0, 16, 16], "metal": [32, 0, 16, 16], "wheel": [48, 0, 16, 16], "bark": [0, 16, 16, 48]}


class PropArchetypeTest(unittest.TestCase):
    def _single(self, part):
        mesh = prop.Mesh()
        prop.BUILDERS[part["kind"]](mesh, TILES, part, (0.0, 0.0, 0.0))
        return mesh

    def test_beam_is_closed_and_wound_outward(self):
        cases = [
            ((0, 0, 2), (10, 0, 2), (0, 0, 1)),     # along +x
            ((3, -4, 1), (-5, 6, 9), (0, 0, 1)),     # oblique
            ((0, 0, 0), (0, 0, 12), (0, 0, 1)),      # vertical: up hint parallel, fallback side
            ((8, 2, 3), (-2, 2, 3), (0, 0, 1)),      # along -x
        ]
        for p0, p1, up in cases:
            with self.subTest(p0=p0, p1=p1):
                mesh = self._single({"kind": "beam", "tile": "wood", "p0": p0, "p1": p1, "size0": [1.5, 1.0], "size1": [1.0, 0.75], "up": up})
                self.assertEqual(len(mesh.F), 6)
                _assert_outward_convex(self, _faces(mesh))

    def test_beam_end_caps_face_away_from_the_beam(self):
        p0, p1 = np.array((0.0, 0.0, 2.0)), np.array((10.0, 0.0, 2.0))
        mesh = self._single({"kind": "beam", "tile": "wood", "p0": p0.tolist(), "p1": p1.tolist(), "size0": [1, 1]})
        caps = _faces(mesh)[4:]
        d = (p1 - p0) / np.linalg.norm(p1 - p0)
        self.assertLess(np.dot(_newell(caps[0]), d), 0.0)  # p0 cap faces -d
        self.assertGreater(np.dot(_newell(caps[1]), d), 0.0)  # p1 cap faces +d

    def test_capped_lathe_is_closed_and_wound_outward(self):
        for axis in ((0, 0, 1), (1, 0, 0), (0.3, -0.5, 0.8)):
            with self.subTest(axis=axis):
                part = {"kind": "lathe", "tile": "wood", "origin": [0, 0, 1], "axis": list(axis),
                        "profile": [[0, 3], [2, 3.6], [6, 3.6], [8, 3]], "sides": 12,
                        "cap_start": "lid", "cap_end": "lid"}
                _assert_outward_convex(self, _faces(self._single(part)))

    def test_pointed_lathe_closes_itself_outward(self):
        part = {"kind": "lathe", "tile": "metal", "axis": [0, 0, 1], "profile": [[0, 0], [1, 2], [5, 2], [6, 0]], "sides": 8}
        _assert_outward_convex(self, _faces(self._single(part)))

    def test_lathe_segment_tile_override(self):
        part = {"kind": "lathe", "tile": "wood", "axis": [1, 0, 0], "profile": [[0, 1], [4, 1], [5, 0.6]],
                "sides": 6, "seg_tiles": {"1": "metal"}}
        mesh = self._single(part)
        x, y, w, h = TILES["metal"]
        uv = np.asarray(mesh.UV)[-2 * 7:]
        self.assertTrue(np.all((uv[:, 0] >= x) & (uv[:, 0] <= x + w) & (uv[:, 1] >= y) & (uv[:, 1] <= y + h)))

    def test_disc_is_a_fan(self):
        mesh = self._single({"kind": "disc", "tile": "wheel", "centre": [0, 4, 3], "axis": [0, 1, 0], "radius": 3, "sides": 10})
        self.assertEqual(archetypes.triangle_count(mesh), 10)
        self.assertEqual(len(mesh.V), 11)

    def test_mirror_and_offset(self):
        params = {"offset": [1, 0, 0], "parts": [
            {"kind": "beam", "tile": "wood", "p0": [0, 3, 0], "p1": [0, 3, 6], "size0": [0.5, 0.5], "mirror": "y", "offset": [0, 0, 2]},
        ]}
        mesh = prop.build(params, TILES)
        self.assertEqual(len(mesh.F), 12)
        V = np.asarray(mesh.V)
        half = len(V) // 2
        self.assertAlmostEqual(V[:half, 1].mean(), 3.0)
        self.assertAlmostEqual(V[half:, 1].mean(), -3.0)
        self.assertAlmostEqual(V[:, 0].mean(), 1.0)
        self.assertAlmostEqual(V[:, 2].min(), 2.0)
        # A mirrored copy is still wound outward.
        _assert_outward_convex(self, _faces(mesh, 6))

    def test_uvs_stay_inside_their_tiles(self):
        params = {"parts": [
            {"kind": "beam", "tile": "wood", "p0": [0, 0, 0], "p1": [40, 0, 0], "size0": [12, 12]},
            {"kind": "lathe", "tile": "wood", "axis": [0, 0, 1], "profile": [[0, 2], [5, 2]], "cap_end": "lid"},
        ]}
        mesh = prop.build(params, TILES)
        for face, kind in zip(mesh.F, mesh.K):
            for vi in face:
                u, v = mesh.UV[vi]
                inside = any(x <= u <= x + w and y <= v <= y + h for x, y, w, h in TILES.values())
                self.assertTrue(inside, (kind, u, v))


class PropAtlasTest(unittest.TestCase):
    SPEC = {"size": [64, 64], "seed": 7, "tiles": [
        {"name": "staves", "rect": [0, 0, 32, 32], "kind": "planks", "hist": {"20": 5, "21": 9, "22": 4},
         "bands": [{"v": [0.2, 0.3], "hist": {"60": 3, "61": 2}}]},
        {"name": "lid", "rect": [32, 0, 16, 16], "kind": "lid", "hist": {"20": 4, "21": 4}, "rim_hist": {"60": 1}},
        {"name": "iron", "rect": [48, 0, 16, 16], "kind": "metal", "hist": {"60": 3, "61": 5, "62": 2}},
        {"name": "wood", "rect": [32, 16, 16, 16], "kind": "grain", "hist": {"30": 3, "31": 3}},
        {"name": "wheel", "rect": [0, 32, 32, 32], "kind": "wheel", "hist": {"30": 3, "31": 3}, "spokes": 6},
        {"name": "plain", "rect": [48, 16, 16, 16], "kind": "solid", "hist": {"40": 1, "41": 1}},
    ]}

    def test_paints_every_kind_from_the_histograms(self):
        rgba, tiles, indices = prop_atlas.paint({"atlas": self.SPEC}, _palette())
        self.assertEqual(rgba.shape, (64, 64, 4))
        self.assertEqual(set(np.unique(rgba[..., 3])) - {0, 255}, set())
        for tile in self.SPEC["tiles"]:
            x, y, w, h = tile["rect"]
            self.assertEqual(tiles[tile["name"]], [x, y, w, h])
            used = set(np.unique(indices[y : y + h, x : x + w])) - {0}
            allowed = {int(k) for key in ("hist", "rim_hist", "hub_hist") for k in (tile.get(key) or {})}
            allowed |= {int(k) for band in tile.get("bands") or [] for k in band["hist"]}
            self.assertTrue(used, tile["name"])
            self.assertLessEqual(used, allowed, tile["name"])
        # The wheel is a cut-out: some texels between spokes are holes.
        x, y, w, h = tiles["wheel"]
        self.assertGreater(np.count_nonzero(rgba[y : y + h, x : x + w, 3] == 0), 0)

    def test_is_deterministic(self):
        a = prop_atlas.paint({"atlas": self.SPEC}, _palette())[0]
        b = prop_atlas.paint({"atlas": copy.deepcopy(self.SPEC)}, _palette())[0]
        self.assertTrue(np.array_equal(a, b))

    def test_rejects_a_tile_outside_the_atlas(self):
        spec = copy.deepcopy(self.SPEC)
        spec["tiles"][0]["rect"] = [40, 40, 32, 32]
        with self.assertRaises(ValueError):
            prop_atlas.paint({"atlas": spec}, _palette())

    def test_rejects_an_unknown_kind(self):
        spec = copy.deepcopy(self.SPEC)
        spec["tiles"][0]["kind"] = "velvet"
        with self.assertRaises(ValueError):
            prop_atlas.paint({"atlas": spec}, _palette())


TREE_HISTS = {
    "top_hist": {"142": 90, "143": 300, "144": 400, "145": 450, "146": 360},
    "bark_hist": {"34": 170, "35": 170, "36": 70, "12": 55, "13": 40},
}


class ConiferPaintersTest(unittest.TestCase):
    def _compact(self, **extra):
        params = _pine_params()
        params["atlas"] = {"layout": "compact128", "size": [128, 128], "seed": 5, **extra}
        return conifer_atlas.paint(params, _palette())

    def test_compact128_layout(self):
        rgba, tiles, indices = self._compact()
        self.assertEqual(rgba.shape, (128, 128, 4))
        for name, rect in conifer_atlas.COMPACT128_TILES.items():
            self.assertEqual(list(tiles[name]), list(rect), name)
        for alias, src in conifer_atlas.COMPACT128_ALIASES.items():
            self.assertEqual(tiles[alias], list(tiles[src]))
        x, y, w, h = tiles["bark"]
        bark = set(np.unique(indices[y : y + h, x : x + w])) - {0}
        self.assertLessEqual(bark, set(conifer_atlas.DEFAULT_BARK_HIST))

    def test_compact128_rejects_other_sizes(self):
        params = {"atlas": {"layout": "compact128", "size": [256, 256]}}
        with self.assertRaises(ValueError):
            conifer_atlas.paint(params, _palette())

    def test_tree128_paints_the_conifer_tile_set(self):
        params = _pine_params()
        params["atlas"] = {"painter": "tree128", "size": [128, 128], **TREE_HISTS}
        rgba, tiles, indices = tree_atlas.paint(params, _palette())
        self.assertEqual(rgba.shape, (128, 128, 4))
        x, y, w, h = tiles["bark"]
        self.assertLessEqual(set(np.unique(indices[y : y + h, x : x + w])) - {0}, {34, 35, 36, 12, 13})
        # The conifer builds against the tree128 tiles.
        mesh = conifer.build(params, tiles)
        self.assertGreater(archetypes.triangle_count(mesh), 0)

    def test_tree128_needs_its_histograms(self):
        with self.assertRaises(ValueError):
            tree_atlas.paint({"atlas": {"painter": "tree128", "top_hist": TREE_HISTS["top_hist"]}}, _palette())

    def test_unknown_bark_key_order_is_a_clear_error(self):
        cases = {
            "conifer": lambda order: conifer_atlas.paint({**_pine_params(), "atlas": {"bark_key_order": order}}, _palette()),
            "compact128": lambda order: self._compact(bark_key_order=order),
            "tree128": lambda order: tree_atlas.paint({"atlas": {"painter": "tree128", "bark_key_order": order, **TREE_HISTS}}, _palette()),
        }
        for painter, run in cases.items():
            with self.subTest(painter=painter):
                with self.assertRaises(ValueError) as ctx:
                    run([34, 35, 99])
                self.assertIn("bark_key_order", str(ctx.exception))
                self.assertIn("99", str(ctx.exception))
                self.assertNotIsInstance(ctx.exception, KeyError)

    def test_known_bark_key_order_still_paints(self):
        rgba, tiles, indices = self._compact(bark_key_order=[11, 12, 34])
        x, y, w, h = tiles["bark"]
        self.assertEqual(set(np.unique(indices[y : y + h, x : x + w])) - {0}, {11, 12, 34})

    def test_unknown_painter_is_rejected(self):
        with self.assertRaises(ValueError):
            archetypes.write_atlas("conifer", {"atlas": {"painter": "oak"}}, _palette(), os.devnull, None)


class ConiferPartsTest(unittest.TestCase):
    def test_trunk_winds_outward(self):
        mesh = conifer.Mesh()
        rings = conifer.trunk_rings({})
        conifer.add_trunk(mesh, TILES, rings)
        _assert_outward_from_axis(self, _faces(mesh), [(0, 0, rings[0][0] - 1), (0, 0, rings[-1][0] + 1)])

    def test_add_stem_geometry_and_winding(self):
        stem = {"points": [[0, 0, 0], [3, 1, 20], [5, 4, 40]], "radii": [2.0, 1.4, 0.0], "sides": 6}
        mesh = conifer.Mesh()
        conifer.add_stem(mesh, TILES, stem)
        # Two segments of 6 sides: one of quads, one closing to a point.
        self.assertEqual(len(mesh.F), 12)
        self.assertEqual(archetypes.triangle_count(mesh), 6 * 2 + 6)
        self.assertEqual(set(mesh.K), {"bark"})
        _assert_outward_from_axis(self, _faces(mesh), stem["points"])
        # V restarts at the tile top rather than running off a 48-texel tile.
        x, y, w, h = TILES["bark"]
        uv = np.asarray(mesh.UV)
        self.assertTrue(np.all((uv[:, 1] >= y) & (uv[:, 1] <= y + h)))

    def test_add_stem_custom_tile_and_kind(self):
        mesh = conifer.Mesh()
        conifer.add_stem(mesh, TILES, {"points": [[0, 0, 0], [0, 0, 5]], "radii": [1, 1], "tile": "wood", "kind": "branch"})
        self.assertEqual(set(mesh.K), {"branch"})
        _assert_outward_from_axis(self, _faces(mesh), [[0, 0, 0], [0, 0, 5]])

    def test_build_adds_stems(self):
        params = _pine_params()
        tiles = conifer_atlas.paint(params, _palette())[1]
        base = archetypes.triangle_count(conifer.build(params, tiles))
        params["stems"] = [{"points": [[0, 0, 0], [6, 0, 30]], "radii": [1.5, 0.0], "sides": 5}]
        self.assertEqual(archetypes.triangle_count(conifer.build(params, tiles)), base + 5)

    def test_clump_root_hangs_the_limb_off_the_side_stem(self):
        params = _pine_params()
        tiles = conifer_atlas.paint(params, _palette())[1]
        params["quality"] = {**(params.get("quality") or {}), "sub": 0}
        params["clumps"] = [dict(params["clumps"][0])]
        params.pop("branches", None)

        def limb_start(p):
            mesh = conifer.build(p, tiles)
            trunk_vertices = (len(conifer.trunk_rings(p))) * (int(conifer.DEFAULT_QUALITY["trunk_sides"]) + 1)
            return np.asarray(mesh.V[trunk_vertices : trunk_vertices + 4]).mean(axis=0), mesh

        plain, plain_mesh = limb_start(params)
        rooted = copy.deepcopy(params)
        rooted["clumps"][0]["root"] = [-6.0, 4.0, 1.5]
        start, rooted_mesh = limb_start(rooted)
        # Without a root the limb leaves the main trunk near the axis.
        self.assertLess(math.hypot(plain[0], plain[1]), 4.0)
        # With one it leaves the side stem at (rx, ry), within its radius.
        self.assertLess(math.hypot(start[0] + 6.0, start[1] - 4.0), 1.5 + 1.0)
        self.assertEqual(archetypes.triangle_count(plain_mesh), archetypes.triangle_count(rooted_mesh))


class SmokeBuildTest(unittest.TestCase):
    """One pure-Python build per archetype through the dispatcher."""

    def test_conifer_pine(self):
        params = _pine_params()
        tiles = conifer_atlas.paint(params, _palette())[1]
        mesh = archetypes.build_mesh("conifer", params, tiles)
        self.assertEqual(archetypes.triangle_count(mesh), 864)  # the approved pine
        self._sane(mesh, tiles)

    def test_prop(self):
        params = {"atlas": PropAtlasTest.SPEC, "parts": [
            {"kind": "lathe", "tile": "staves", "axis": [0, 0, 1], "profile": [[0, 4], [3, 4.6], [7, 4]], "sides": 10,
             "cap_start": "lid", "cap_end": "lid"},
            {"kind": "beam", "tile": "wood", "p0": [-6, 3, 0], "p1": [6, 3, 1], "size0": [0.6, 0.6], "mirror": "y"},
            {"kind": "disc", "tile": "wheel", "centre": [0, 5, 2], "axis": [0, 1, 0], "radius": 2},
        ]}
        tiles = prop_atlas.paint(params, _palette())[1]
        mesh = archetypes.build_mesh("prop", params, tiles)
        self.assertEqual(archetypes.triangle_count(mesh), prop.triangle_count(mesh))
        self.assertGreater(archetypes.triangle_count(mesh), 0)
        self._sane(mesh, tiles)

    def test_unknown_archetype(self):
        with self.assertRaises(ValueError):
            archetypes.build_mesh("oak", {}, {})

    def test_triangle_count_is_archetype_neutral(self):
        self.assertEqual(archetypes.triangle_count.__module__, "archetypes")
        mesh = conifer.Mesh()
        mesh.f((0, 1, 2), "a")
        mesh.f((0, 1, 2, 3), "a")
        mesh.f((0, 1, 2, 3, 4), "a")
        self.assertEqual(archetypes.triangle_count(mesh), 1 + 2 + 3)
        self.assertEqual(archetypes.triangle_count(mesh), conifer.triangle_count(mesh))

    def _sane(self, mesh, tiles):
        V = np.asarray(mesh.V, float)
        self.assertTrue(np.all(np.isfinite(V)))
        self.assertEqual(len(mesh.V), len(mesh.UV))
        self.assertEqual(len(mesh.F), len(mesh.K))
        for face in mesh.F:
            self.assertGreaterEqual(len(face), 3)
            self.assertTrue(all(0 <= i < len(V) for i in face))
        size = max(x + w for x, _y, w, _h in tiles.values()), max(y + h for _x, y, _w, h in tiles.values())
        uv = np.asarray(mesh.UV, float)
        self.assertTrue(np.all((uv >= 0) & (uv <= np.array(size))))


if __name__ == "__main__":
    unittest.main()
