# Copyright (c) 2026 OpenRCT2 developers
# Licensed under the GNU General Public License version 3.

"""Closed-mask IoU gate (batch 1 QA Decision 1)."""

from __future__ import annotations

import unittest

import numpy as np

import conventions as C
import rebuild
import validate
from render_views import close_mask, closed_silhouette_iou, silhouette_iou


def _rgba(mask):
    out = np.zeros(mask.shape + (4,), np.uint8)
    out[mask] = (10, 20, 30, 255)
    return out


def _reference_close(mask, r):
    """Independent close: pad, scan every pixel's (2r+1)^2 window."""
    pad = r + 2
    m = np.pad(mask, pad)
    h, w = m.shape
    dil = np.zeros_like(m)
    for y in range(h):
        for x in range(w):
            dil[y, x] = m[max(y - r, 0) : y + r + 1, max(x - r, 0) : x + r + 1].any()
    ero = np.zeros_like(m)
    for y in range(h):
        for x in range(w):
            win = dil[y - r : y + r + 1, x - r : x + r + 1] if r <= y < h - r and r <= x < w - r else None
            ero[y, x] = bool(win is not None and win.all())
    return ero[pad:-pad, pad:-pad]


class CloseMaskTest(unittest.TestCase):
    def test_conventions(self):
        self.assertEqual(C.IOU_CLOSE_KERNEL, 3)
        self.assertEqual(C.IOU_CLOSED_MIN_MEAN, 0.75)
        self.assertEqual(C.IOU_CLOSED_MIN_PER_VIEW, 0.70)
        # Raw IoU is reported only; there is no raw threshold.
        self.assertFalse(hasattr(C, "IOU_MIN_PER_VIEW") or hasattr(C, "IOU_MIN_MEAN"))

    def test_fills_single_pixel_holes(self):
        mask = np.zeros((12, 12), bool)
        mask[3:9, 3:9] = True
        mask[5, 5] = False
        mask[6, 7] = False
        closed = close_mask(mask, 3)
        self.assertTrue(closed[5, 5] and closed[6, 7])
        self.assertTrue(np.array_equal(closed, np.pad(np.ones((6, 6), bool), 3)))

    def test_matches_a_reference_close(self):
        rng = np.random.default_rng(7)
        for _ in range(5):
            mask = rng.random((20, 24)) > 0.55
            self.assertTrue(np.array_equal(close_mask(mask, 3), _reference_close(mask, 1)))

    def test_shape_touching_the_edge_is_not_eroded(self):
        mask = np.zeros((8, 8), bool)
        mask[:, :4] = True
        self.assertTrue(np.array_equal(close_mask(mask, 3), mask))

    def test_kernel_1_is_identity(self):
        mask = np.random.default_rng(1).random((10, 10)) > 0.5
        self.assertTrue(np.array_equal(close_mask(mask, 1), mask))

    def test_stipple_scores_low_raw_and_high_closed(self):
        solid = np.zeros((40, 40), bool)
        solid[8:32, 8:32] = True
        stipple = solid.copy()
        stipple[::2, ::2] &= False  # a quarter of the pixels dropped, as RCT2 dithers
        raw, _s, _r = silhouette_iou(_rgba(stipple), _rgba(solid))
        closed = closed_silhouette_iou(_rgba(stipple), _rgba(solid), 3)
        self.assertLess(raw, 0.8)
        self.assertGreater(closed, 0.99)


class GateTest(unittest.TestCase):
    def _scores(self, closed_views, raw_views=None):
        raw_views = raw_views or [0.6] * 4
        return {
            "raw": {"views": raw_views, "mean": float(np.mean(raw_views))},
            "closed": {"views": closed_views, "mean": float(np.mean(closed_views)), "kernel_px": 3},
            "sprite_px": [1000] * 4,
            "render_px": [1000] * 4,
        }

    def test_iou_block_keeps_approved_figures(self):
        approved = {"views": [0.78, 0.74, 0.79, 0.73], "mean": 0.76, "source": "approved fit"}
        block = rebuild.iou_block(approved, self._scores([0.781, 0.737, 0.787, 0.734], [0.73, 0.71, 0.77, 0.69]))
        self.assertEqual(block["views"], approved["views"])
        self.assertEqual(block["mean"], 0.76)
        self.assertEqual(block["source"], "approved fit")
        self.assertEqual(block["raw"]["views"], [0.73, 0.71, 0.77, 0.69])

    def test_iou_block_without_approval_records_the_closed_measurement(self):
        block = rebuild.iou_block(None, self._scores([0.8] * 4, [0.62] * 4))
        self.assertEqual(block["views"], [0.8] * 4)
        self.assertEqual(block["mean"], 0.8)
        self.assertEqual(block["kernel_px"], 3)
        self.assertEqual((block["threshold_per_view"], block["threshold_mean"]), (0.70, 0.75))
        self.assertEqual(block["raw"], {"views": [0.62] * 4, "mean": 0.62})
        self.assertIsNotNone(block["source"])

    def test_iou_block_has_no_legacy_fields(self):
        block = rebuild.iou_block(None, self._scores([0.8] * 4))
        self.assertEqual(set(block), {"views", "mean", "kernel_px", "threshold_per_view", "threshold_mean", "source", "raw"})
        self.assertEqual(set(block["raw"]), {"views", "mean"})

    def test_iou_block_without_sprites(self):
        approved = {"views": [0.75] * 4, "mean": 0.75, "source": "s"}
        self.assertEqual(
            set(rebuild.iou_block(approved, None)),
            {"views", "mean", "kernel_px", "threshold_per_view", "threshold_mean", "source"},
        )

    def test_score_silhouettes_reports_both(self):
        solid = np.zeros((40, 40), bool)
        solid[8:32, 8:32] = True
        stipple = solid.copy()
        stipple[::2, ::2] = False
        scores = validate.score_silhouettes([_rgba(solid)] * 4, [_rgba(stipple)] * 4)
        self.assertLess(scores["raw"]["mean"], scores["closed"]["mean"])
        self.assertEqual(scores["closed"]["kernel_px"], C.IOU_CLOSE_KERNEL)


class DeclaredClosedIouTest(unittest.TestCase):
    """The meta's recorded closed-mask iou.views / iou.mean are held to the gate."""

    def _errors(self, iou):
        report = validate.Report()
        validate.check_declared_closed_iou({"iou": iou}, report)
        return report.errors

    def test_passing_block(self):
        self.assertEqual(self._errors({"views": [0.78, 0.74, 0.79, 0.73], "mean": 0.76}), [])

    def test_low_view_fails(self):
        self.assertEqual(len(self._errors({"views": [0.78, 0.69, 0.79, 0.73], "mean": 0.76})), 1)

    def test_low_mean_fails_even_with_every_view_passing(self):
        errors = self._errors({"views": [0.72, 0.72, 0.72, 0.72], "mean": 0.72})
        self.assertEqual(len(errors), 1)
        self.assertIn("mean", errors[0])

    def test_mean_on_the_threshold_passes(self):
        self.assertEqual(self._errors({"views": [0.75] * 4, "mean": C.IOU_CLOSED_MIN_MEAN}), [])

    def test_missing_block_is_not_an_error_here(self):
        self.assertEqual(self._errors({}), [])
        report = validate.Report()
        validate.check_declared_closed_iou({}, report)
        self.assertEqual(report.errors, [])

    def test_checked_without_sprites(self):
        import os

        if not os.path.isfile(validate.FLAT_GLB):
            self.skipTest("committed pine GLB not in this checkout")
        import json

        meta = json.load(open(validate.FLAT_META))
        meta["iou"]["views"] = [0.78, 0.60, 0.79, 0.73]
        report = validate.Report()
        validate.render_and_score(validate.load_mesh(validate.FLAT_GLB), meta, None, None, report, False)
        self.assertTrue(any("iou.views" in e for e in report.errors), report.errors)


class SparseCrownTest(unittest.TestCase):
    """crown: "sparse" adds area <= 1.10 and raw >= 0.50 in every view."""

    def test_pass(self):
        self.assertEqual(validate.sparse_crown_status([1.09, 1.08, 1.10, 1.0], [0.51, 0.52, 0.5, 0.6])[0], "PASS")

    def test_area_over_fails(self):
        self.assertEqual(validate.sparse_crown_status([1.11, 1.0, 1.0, 1.0], [0.6] * 4)[0], "FAIL")

    def test_raw_under_floor_fails(self):
        self.assertEqual(validate.sparse_crown_status([1.0] * 4, [0.6, 0.49, 0.6, 0.6])[0], "FAIL")

    def test_unknown_crown_kind_is_a_schema_error(self):
        report = validate.Report()
        validate.check_meta_schema({"crown": "dense"}, report)
        self.assertTrue(any("crown" in e for e in report.errors), report.errors)


class UvsWithoutImageTest(unittest.TestCase):
    """A GLB with UVs but no embedded image FAILs cleanly instead of raising."""

    def test_validator_fails_without_a_traceback(self):
        import os
        import tempfile
        from unittest import mock

        if not os.path.isfile(validate.FLAT_GLB):
            self.skipTest("committed pine GLB not in this checkout")
        mesh = validate.load_mesh(validate.FLAT_GLB)
        self.assertIsNotNone(mesh["uvs"])
        mesh["image"] = None
        with tempfile.TemporaryDirectory() as tmp, mock.patch.object(validate, "load_mesh", return_value=mesh):
            report = validate.validate_files(validate.FLAT_GLB, validate.FLAT_META, None, os.path.join(tmp, "r.png"), False)
        self.assertFalse(report.ok())
        failed = [name for name, status, _detail in report.rows if status == "FAIL"]
        self.assertTrue(any("image" in name for name in failed), failed)
        self.assertFalse(any("could not read GLB" in e for e in report.errors))


if __name__ == "__main__":
    unittest.main()
