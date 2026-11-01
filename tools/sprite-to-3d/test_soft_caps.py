# Copyright (c) 2026 OpenRCT2 developers
# Licensed under the GNU General Public License version 3.

"""Triangle budgets (batch 1 QA Decision 5): soft_max_tris warns, hard_tris fails, the tree floor warns.

Every number is read from conventions.SIZE_CLASSES.
"""

from __future__ import annotations

import os
import tempfile
import unittest

import conventions as C
import validate


def _fake_mesh(triangles):
    gltf = {
        "asset": {"version": "2.0"},
        "nodes": [{"name": "n", "mesh": 0}],
        "meshes": [{"primitives": [{"attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2}, "indices": 3, "mode": 4}]}],
        "materials": [{}],
        "accessors": [{}, {}, {}, {"componentType": 5123}],
    }
    return {"gltf": gltf, "positions": [(0.0, 0.0, 0.0)] * 3, "triangles": triangles, "image": None}


def _check(size_class, triangles, **meta_extra):
    report = validate.Report()
    meta = {"object_id": "test.soft_caps", "size_class": size_class}
    meta.update(meta_extra)
    with tempfile.TemporaryDirectory() as tmp:
        glb = os.path.join(tmp, "test.soft_caps.glb")
        with open(glb, "wb") as fh:
            fh.write(b"\0" * 64)  # only its size is read
        validate.apply_checklist(report, glb, meta, _fake_mesh(triangles))
    return report


def _rows(size_class, triangles, **meta_extra):
    report = _check(size_class, triangles, **meta_extra)
    return report, {name: status for name, status, _detail in report.rows}


def _detail(report, row):
    return next(detail for name, _status, detail in report.rows if name == row)


FULL_TREES = ("tree_small", "tree_1tile", "tree_large")


class SoftCapTest(unittest.TestCase):
    def test_soft_max_tris_values(self):
        expected = {
            "prop_quarter": 250,
            "prop_tile": 500,
            "tree_quarter": 400,
            "tree_small": 1000,
            "tree_1tile": 1200,
            "tree_large": 1200,
            "large_scenery": 1200,
        }
        self.assertEqual({k: v["soft_max_tris"] for k, v in C.SIZE_CLASSES.items()}, expected)

    def test_no_inferred_soft_cap_logic_left(self):
        self.assertFalse(hasattr(validate, "soft_triangle_cap"))
        self.assertFalse(hasattr(validate, "check_triangles"))
        for name, budget in C.SIZE_CLASSES.items():
            self.assertNotIn("soft_tris", budget, name)

    def test_every_soft_cap_is_below_its_hard_cap(self):
        for name, budget in C.SIZE_CLASSES.items():
            self.assertLess(budget["soft_max_tris"], budget["hard_tris"], name)

    def test_soft_cap_boundary_for_every_class(self):
        for name, budget in C.SIZE_CLASSES.items():
            soft = budget["soft_max_tris"]
            _report, rows = _rows(name, soft)
            self.assertEqual(rows["triangles <= soft cap"], "PASS", name)
            report, rows = _rows(name, soft + 1)
            self.assertEqual(rows["triangles <= soft cap"], "WARN", name)
            self.assertEqual(rows["triangles <= hard cap"], "PASS", name)
            self.assertTrue(any("soft cap" in w for w in report.warnings), name)
            self.assertFalse(any("hard cap" in e or "soft cap" in e for e in report.errors), name)

    def test_hard_cap_boundary_for_every_class(self):
        for name, budget in C.SIZE_CLASSES.items():
            hard = budget["hard_tris"]
            _report, rows = _rows(name, hard)
            self.assertEqual(rows["triangles <= hard cap"], "PASS", name)
            report, rows = _rows(name, hard + 1)
            self.assertEqual(rows["triangles <= hard cap"], "FAIL", name)
            self.assertTrue(any("hard cap" in e for e in report.errors), name)

    def test_soft_cap_is_reported_once(self):
        report = _check("prop_quarter", 280)
        names = [name for name, _s, _d in report.rows]
        self.assertEqual(names.count("triangles <= soft cap"), 1)
        self.assertEqual(sum("soft cap" in w for w in report.warnings), 1)

    def test_full_tree_floor_is_600(self):
        for name in FULL_TREES:
            self.assertEqual(C.SIZE_CLASSES[name]["tree_warn_below"], 600, name)
            _report, rows = _rows(name, 599)
            self.assertEqual(rows["triangle target"], "WARN", name)
            _report, rows = _rows(name, 600)
            self.assertEqual(rows["triangle target"], "PASS", name)

    def test_tree_quarter_floor_is_200(self):
        _report, rows = _rows("tree_quarter", 199)
        self.assertEqual(rows["triangle target"], "WARN")
        _report, rows = _rows("tree_quarter", 200)
        self.assertEqual(rows["triangle target"], "PASS")

    def test_props_have_no_floor(self):
        for name in ("prop_quarter", "prop_tile", "large_scenery"):
            _report, rows = _rows(name, 10)
            self.assertEqual(rows["triangle target"], "PASS", name)


class TriangleJustificationTest(unittest.TestCase):
    """Batch 1 QA Decision 5, condition 4: prop_quarter stays at 250; a justification annotates the WARN."""

    def test_justification_annotates_but_keeps_the_warning(self):
        reason = "two stacked flower tiers; the bush reads flat below 280"
        report, rows = _rows("prop_quarter", 280, triangle_justification=reason)
        self.assertEqual(rows["triangles <= soft cap"], "WARN")
        self.assertIn(reason, _detail(report, "triangles <= soft cap"))
        self.assertFalse(any("triangles" in e for e in report.errors))

    def test_justification_under_the_cap_is_not_shown(self):
        report, rows = _rows("prop_quarter", 240, triangle_justification="not needed")
        self.assertEqual(rows["triangles <= soft cap"], "PASS")
        self.assertNotIn("not needed", _detail(report, "triangles <= soft cap"))

    def test_justification_does_not_lift_the_hard_cap(self):
        report, rows = _rows("prop_quarter", 501, triangle_justification="please")
        self.assertEqual(rows["triangles <= hard cap"], "FAIL")
        self.assertFalse(report.ok())

    def test_blank_justification_is_ignored(self):
        self.assertEqual(validate.triangle_justification({"triangle_justification": "  "}), "")
        self.assertEqual(validate.triangle_justification({}), "")


if __name__ == "__main__":
    unittest.main()
