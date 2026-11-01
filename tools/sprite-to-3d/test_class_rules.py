# Copyright (c) 2026 OpenRCT2 developers
# Licensed under the GNU General Public License version 3.

"""Per-class render floor, footprints, histograms and per-view tips (batch 1 QA Decisions 2-4 and "Other issues" 3)."""

from __future__ import annotations

import os
import tempfile
import unittest
from unittest import mock

import numpy as np

import conventions as C
import rebuild
import validate


def _blender_to_gltf(points):
    return np.array([(x, z, -y) for x, y, z in points], np.float64)


def _fake_mesh(blender_points, indices=None):
    gltf = {
        "asset": {"version": "2.0"},
        "nodes": [{"name": "n", "mesh": 0}],
        "meshes": [{"primitives": [{"attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2}, "indices": 3, "mode": 4}]}],
        "materials": [{}],
        "accessors": [{}, {}, {}, {"componentType": 5123}],
    }
    mesh = {"gltf": gltf, "positions": _blender_to_gltf(blender_points), "triangles": 300, "image": None}
    if indices is not None:
        mesh["indices"] = np.asarray(indices, np.int64)
    return mesh


def _rows(size_class, mesh, **meta_extra):
    report = validate.Report()
    meta = {"object_id": "test.class_rules", "size_class": size_class}
    meta.update(meta_extra)
    with tempfile.TemporaryDirectory() as tmp:
        glb = os.path.join(tmp, "x.glb")
        with open(glb, "wb") as fh:
            fh.write(b"\0" * 64)
        validate.apply_checklist(report, glb, meta, mesh)
    return report, {name: (status, detail) for name, status, detail in report.rows}


def _box(half, z0=0.0, z1=1.0):
    return [(sx * half, sy * half, z) for sx in (-1, 1) for sy in (-1, 1) for z in (z0, z1)]


class MinRenderPxTest(unittest.TestCase):
    def test_values_per_class(self):
        expected = {"prop_quarter": 100, "prop_tile": 250, "tree_quarter": 150}
        for name, budget in C.SIZE_CLASSES.items():
            self.assertEqual(budget["min_render_px"], expected.get(name, 500), name)
            self.assertEqual(validate.min_render_px({"size_class": name}), expected.get(name, 500), name)
        self.assertEqual(validate.min_render_px({"size_class": "nonsense"}), C.DEFAULT_MIN_RENDER_PX)

    def test_no_prefix_hack(self):
        source = open(validate.__file__, encoding="utf-8").read()
        self.assertNotIn('startswith("prop")', source)

    def test_render_floor_is_read_per_class(self):
        # A small square renders a few hundred pixels: fine for prop_quarter and
        # prop_tile, an empty-render error for the 500 px classes.
        # Two crossed cards, so every view sees one face-on.
        points = [(-0.15, -0.15, 0.0), (0.15, 0.15, 0.0), (0.15, 0.15, 0.5), (-0.15, -0.15, 0.5)]
        points += [(-0.15, 0.15, 0.0), (0.15, -0.15, 0.0), (0.15, -0.15, 0.5), (-0.15, 0.15, 0.5)]
        mesh = {
            "positions": _blender_to_gltf(points),
            "uvs": None,
            "indices": np.array([0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7]),
            "image": None,
        }
        coverage = None
        results = {}
        for name in ("prop_quarter", "prop_tile", "tree_quarter", "tree_1tile"):
            report = validate.Report()
            validate.render_and_score(mesh, {"size_class": name}, None, None, report, False)
            results[name] = any("opaque pixels" in e for e in report.errors)
            coverage = next(n for n in report.notes if n.startswith("render coverage"))
        pixels = [int(c) for c in coverage.split(" ", 2)[2].split(", ")]
        self.assertTrue(all(250 <= p < 500 for p in pixels), coverage)
        self.assertFalse(results["prop_quarter"])
        self.assertFalse(results["prop_tile"])
        self.assertFalse(results["tree_quarter"])
        self.assertTrue(results["tree_1tile"])


class MinRenderPxFallbackTest(unittest.TestCase):
    def test_zero_floor_is_kept(self):
        # An `or` fallback turned a class floor of 0 into DEFAULT_MIN_RENDER_PX.
        classes = dict(C.SIZE_CLASSES)
        classes["zero_floor"] = dict(C.SIZE_CLASSES["prop_tile"], min_render_px=0)
        with mock.patch.object(C, "SIZE_CLASSES", classes):
            self.assertEqual(validate.min_render_px({"size_class": "zero_floor"}), 0)

    def test_missing_floor_falls_back(self):
        classes = dict(C.SIZE_CLASSES)
        budget = dict(C.SIZE_CLASSES["prop_tile"])
        budget.pop("min_render_px")
        classes["no_floor"] = budget
        with mock.patch.object(C, "SIZE_CLASSES", classes):
            self.assertEqual(validate.min_render_px({"size_class": "no_floor"}), C.DEFAULT_MIN_RENDER_PX)

    def test_unknown_class_uses_the_default(self):
        self.assertEqual(validate.min_render_px({"size_class": "nope"}), C.DEFAULT_MIN_RENDER_PX)


class OriginRowTest(unittest.TestCase):
    def test_full_tile_says_tile_centre(self):
        _report, rows = _rows("prop_tile", _fake_mesh(_box(0.3)))
        self.assertEqual(rows["origin inside footprint"], ("PASS", "tile centre"))

    def test_quarter_classes_say_quadrant_centre(self):
        for size_class in ("prop_quarter", "tree_quarter"):
            with self.subTest(size_class=size_class):
                _report, rows = _rows(size_class, _fake_mesh(_box(0.2)))
                self.assertEqual(rows["origin inside footprint"], ("PASS", "quadrant centre"))


class HistogramKeyTest(unittest.TestCase):
    def test_one_definition(self):
        for key in ("hist", "sprite_hist", "rim_hist", "bark_hist"):
            self.assertTrue(C.is_histogram_key(key), key)
        for key in ("history", "hist_size", "tiles", 3):
            self.assertFalse(C.is_histogram_key(key), key)

    def test_rebuild_and_validate_share_it(self):
        self.assertFalse(hasattr(rebuild, "_is_hist_key"))
        value = {"atlas": {"hist": {"1": 2}, "tiles": [{"rim_hist": {}}]}, "x_hist": [1]}
        paths = [path for path, _item in validate._hist_paths(value)]
        self.assertEqual(sorted(paths), ["atlas.hist", "atlas.tiles[0].rim_hist", "x_hist"])
        self.assertEqual(rebuild.strip_histograms(value), {"atlas": {"tiles": [{}]}})
        with mock.patch.object(C, "is_histogram_key", lambda key: key == "tiles"):
            self.assertEqual([p for p, _ in validate._hist_paths(value)], ["atlas.tiles"])
            self.assertNotIn("tiles", rebuild.strip_histograms(value)["atlas"])


class FootprintTest(unittest.TestCase):
    def test_tree_quarter_budget(self):
        b = C.SIZE_CLASSES["tree_quarter"]
        self.assertEqual((b["soft_max_tris"], b["hard_tris"], b["tree_warn_below"]), (400, 600, 200))
        self.assertEqual(b["max_texture"], 128)
        self.assertEqual(tuple(b["footprint_tiles"]), (0.5, 0.5))
        self.assertEqual(b["footprint_bu"], 0.40)
        self.assertEqual(b["trunk_base_bu"], 0.25)
        self.assertFalse(b["canopy"])

    def test_prop_quarter_within_a_quarter_unit(self):
        _r, rows = _rows("prop_quarter", _fake_mesh(_box(0.24)))
        self.assertEqual(rows["footprint"][0], "PASS")
        _r, rows = _rows("prop_quarter", _fake_mesh(_box(0.30)))
        self.assertEqual(rows["footprint"][0], "FAIL")
        self.assertNotIn("trunk base footprint", rows)

    def test_tree_quarter_canopy_and_trunk(self):
        trunk = _box(0.08, 0.0, 1.0)
        canopy = _box(0.38, 1.0, 2.0)
        _r, rows = _rows("tree_quarter", _fake_mesh(trunk + canopy))
        self.assertEqual(rows["footprint"][0], "PASS")
        self.assertEqual(rows["trunk base footprint"][0], "PASS")
        _r, rows = _rows("tree_quarter", _fake_mesh(_box(0.08) + _box(0.45, 1.0, 2.0)))
        self.assertEqual(rows["footprint"][0], "FAIL")
        _r, rows = _rows("tree_quarter", _fake_mesh(_box(0.30, 0.0, 0.05) + canopy))
        self.assertEqual(rows["trunk base footprint"][0], "FAIL")

    def test_trunk_base_band_is_the_bottom_slice(self):
        self.assertEqual(C.TRUNK_BASE_BAND_SIM, 4.0)
        band = C.TRUNK_BASE_BAND_SIM / C.SIM_PER_UNIT
        # Wide geometry just above the band does not count; just inside does.
        above = _box(0.08) + _box(0.35, band + 0.01, band + 0.5)
        inside = _box(0.08) + _box(0.35, band - 0.01, band + 0.5)
        self.assertAlmostEqual(validate.trunk_base_extent(_blender_to_gltf(above)), 0.08)
        self.assertAlmostEqual(validate.trunk_base_extent(_blender_to_gltf(inside)), 0.35)

    def test_trunk_base_counts_edge_crossings(self):
        # One tall leaning triangle: its only low vertex is at the centre, but at
        # the top of the band it is already 0.3 out.
        band = C.TRUNK_BASE_BAND_SIM / C.SIM_PER_UNIT
        lean = 0.3 / band
        points = [(0.0, 0.0, 0.0), (lean, 0.0, 1.0), (lean, 0.05, 1.0)]
        positions = _blender_to_gltf(points)
        self.assertAlmostEqual(validate.trunk_base_extent(positions), 0.0)
        self.assertAlmostEqual(validate.trunk_base_extent(positions, [0, 1, 2]), 0.3, places=6)

    def test_other_classes_keep_their_footprints(self):
        self.assertEqual(C.SIZE_CLASSES["prop_tile"]["footprint_bu"], C.PROP_FOOTPRINT)
        for name in ("tree_small", "tree_1tile", "tree_large"):
            self.assertEqual(C.SIZE_CLASSES[name]["footprint_bu"], C.TREE_FOOTPRINT, name)


class HistogramTest(unittest.TestCase):
    def test_limit_is_16(self):
        self.assertEqual(C.MAX_HIST_ENTRIES, 16)

    def test_sixteen_entries_pass_seventeen_fail(self):
        ok = {"atlas": {"sprite_hist": {str(i): 1 for i in range(16)}}}
        rebuild.check_histograms(ok)
        bad = {"atlas": {"tiles": [{"hist": {str(i): 1 for i in range(17)}}]}}
        with self.assertRaises(SystemExit) as ctx:
            rebuild.check_histograms(bad)
        self.assertIn("atlas.tiles[0].hist", str(ctx.exception))

    def test_histograms_are_not_copied_into_meta(self):
        params = {
            "atlas": {"size": [128, 128], "sprite_hist": {"1": 2}, "tiles": [{"name": "a", "hist": {"3": 4}}]},
            "rim_hist": {"5": 6},
            "keep": 1,
        }
        stripped = rebuild.strip_histograms(params)
        self.assertEqual(stripped, {"atlas": {"size": [128, 128], "tiles": [{"name": "a"}]}, "keep": 1})
        self.assertIn("sprite_hist", params["atlas"])  # params.json itself is untouched

    def test_validator_flags_histograms_in_meta(self):
        _r, rows = _rows("prop_tile", _fake_mesh(_box(0.2)), params={"atlas": {"top_hist": {"1": 1}}})
        self.assertEqual(rows["no palette histograms in meta"][0], "FAIL")
        _r, rows = _rows("prop_tile", _fake_mesh(_box(0.2)), params={"atlas": {"size": [128, 128]}})
        self.assertEqual(rows["no palette histograms in meta"][0], "PASS")

    def test_example_params_are_within_the_limit(self):
        examples = os.path.join(os.path.dirname(validate.__file__), "examples")
        if not os.path.isdir(examples):
            self.skipTest("no examples folder")
        for name in sorted(os.listdir(examples)):
            path = os.path.join(examples, name, "params.json")
            if os.path.exists(path):
                rebuild.check_histograms(rebuild._read(path))


class PerViewTipTest(unittest.TestCase):
    def test_thresholds(self):
        self.assertEqual((C.TIP_VIEW_WARN_SIM, C.TIP_VIEW_FAIL_SIM), (2.0, 4.0))

    def test_status(self):
        self.assertEqual(validate.per_view_tip_status([0, 1, -2, 2])[0], "PASS")
        self.assertEqual(validate.per_view_tip_status([0, 2.5, 0, 0])[0], "WARN")
        self.assertEqual(validate.per_view_tip_status([0, 4, 4, 3])[0], "WARN")  # the pine
        self.assertEqual(validate.per_view_tip_status([0, -4.5, 0, 0])[0], "FAIL")
        self.assertEqual(validate.per_view_tip_status([5, 0, 0, 0])[0], "FAIL")

    def test_fail_limit_is_inclusive(self):
        # The pine measures +0/+4/+4/+3 and must stay a WARN: exactly
        # TIP_VIEW_FAIL_SIM is on the limit, not over it. Float noise from a
        # fractional anchor row does not tip it into FAIL either.
        limit = C.TIP_VIEW_FAIL_SIM
        self.assertEqual(validate.per_view_tip_status([0, limit, -limit, 0])[0], "WARN")
        self.assertEqual(validate.per_view_tip_status([0, limit + 1e-9, 0, 0])[0], "WARN")
        self.assertEqual(validate.per_view_tip_status([0, limit + 0.01, 0, 0])[0], "FAIL")
        self.assertEqual(validate.per_view_tip_status([0, limit + 1, 0, 0])[0], "FAIL")
        self.assertEqual(validate.per_view_tip_status([C.TIP_VIEW_WARN_SIM] * 4)[0], "PASS")
        self.assertLess(C.TIP_VIEW_EPS, 0.01)

    def test_tallest_check_still_runs(self):
        h = 192
        def column(top):
            m = np.zeros((h, 128), bool)
            m[top:165, 64] = True
            return m
        sprites = [column(t) for t in (25, 29, 29, 28)]
        renders = [column(26)] * 4
        ok, model_tip, tallest, rows = validate.compare_tip_heights(renders, sprites, [-2, 0], -1)
        self.assertTrue(ok)
        self.assertEqual(validate.per_view_tip_status([r[2] for r in rows])[0], "WARN")


if __name__ == "__main__":
    unittest.main()
