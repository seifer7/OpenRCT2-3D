# Copyright (c) 2026 OpenRCT2 developers
# Licensed under the GNU General Public License version 3.

"""Closed-mask IoU gate (batch 1 QA decision 1)."""

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
        # The raw figures stay as the reported reference.
        self.assertEqual((C.IOU_MIN_PER_VIEW, C.IOU_MIN_MEAN), (0.65, 0.70))

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

    def test_iou_block_keeps_approved_raw_figures(self):
        approved = {"views": [0.733, 0.712, 0.766, 0.687], "mean": 0.725, "source": "approved fit"}
        block = rebuild.iou_block(approved, self._scores([0.78, 0.737, 0.787, 0.734], [0.73, 0.71, 0.77, 0.69]))
        self.assertEqual(block["views"], approved["views"])
        self.assertEqual(block["mean"], 0.725)
        self.assertEqual(block["source"], "approved fit")
        self.assertEqual(block["closed"]["views"], [0.78, 0.737, 0.787, 0.734])
        self.assertEqual(block["closed"]["kernel_px"], 3)
        self.assertEqual(block["closed"]["threshold_mean"], 0.75)
        self.assertEqual(block["raw_measured"]["views"], [0.73, 0.71, 0.77, 0.69])

    def test_iou_block_without_approval_records_the_measurement(self):
        block = rebuild.iou_block(None, self._scores([0.8] * 4, [0.62] * 4))
        self.assertEqual(block["views"], [0.62] * 4)
        self.assertEqual(block["mean"], 0.62)
        self.assertIsNotNone(block["source"])

    def test_iou_block_without_sprites_is_unchanged(self):
        approved = {"views": [0.7] * 4, "mean": 0.7, "source": "s"}
        self.assertEqual(
            set(rebuild.iou_block(approved, None)),
            {"views", "mean", "threshold_per_view", "threshold_mean", "source"},
        )

    def test_score_silhouettes_reports_both(self):
        solid = np.zeros((40, 40), bool)
        solid[8:32, 8:32] = True
        stipple = solid.copy()
        stipple[::2, ::2] = False
        scores = validate.score_silhouettes([_rgba(solid)] * 4, [_rgba(stipple)] * 4)
        self.assertLess(scores["raw"]["mean"], scores["closed"]["mean"])
        self.assertEqual(scores["closed"]["kernel_px"], C.IOU_CLOSE_KERNEL)


if __name__ == "__main__":
    unittest.main()
