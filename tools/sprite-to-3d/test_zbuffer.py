# Copyright (c) 2026 OpenRCT2 developers
# Licensed under the GNU General Public License version 3.

"""The validator rasteriser is depth-tested (batch 1 QA renderer finding).

Colour must come from the nearest opaque surface whatever the triangle
order; which pixels are opaque must not depend on the depth test.
"""

from __future__ import annotations

import unittest

import numpy as np

from projection import camera_depth
from render_views import rasterize, vertex_depths


def _blender_to_gltf(p):
    x, y, z = p
    return (x, z, -y)


def _quad(cx, cy, z0, z1, half=0.3):
    """Vertical card at Blender (cx, cy), facing the view-0 camera, from z0 to z1."""
    # View 0 looks along -(1, 1, s); a card spanning (1, -1) faces it.
    a = (cx - half, cy + half, z0)
    b = (cx + half, cy - half, z0)
    c = (cx + half, cy - half, z1)
    d = (cx - half, cy + half, z1)
    return [_blender_to_gltf(p) for p in (a, b, c, d)]


def _scene(order):
    """Two cards, the near one red and the far one blue, drawn in the given order."""
    near = _quad(0.2, 0.2, 0.0, 1.0)
    far = _quad(-0.2, -0.2, 0.0, 1.0)
    positions = np.array(near + far, np.float64)
    # Atlas: left half red, right half blue, all opaque.
    atlas = np.zeros((4, 4, 4), np.uint8)
    atlas[:, :2] = (255, 0, 0, 255)
    atlas[:, 2:] = (0, 0, 255, 255)
    uvs = np.array([[0.2, 0.5]] * 4 + [[0.8, 0.5]] * 4, np.float64)
    tris = {"near": [0, 1, 2, 0, 2, 3], "far": [4, 5, 6, 4, 6, 7]}
    indices = np.array(tris[order[0]] + tris[order[1]], np.int64)
    return positions, uvs, indices, atlas


class DepthTest(unittest.TestCase):
    def test_camera_depth_orders_points_toward_the_camera(self):
        # View 0's camera sits toward +x +y; the nearer point has the smaller depth.
        self.assertLess(camera_depth((0.5, 0.5, 0.0), 0), camera_depth((-0.5, -0.5, 0.0), 0))
        self.assertLess(camera_depth((0.0, 0.0, 1.0), 0), camera_depth((0.0, 0.0, 0.0), 0))

    def test_vertex_depths_match_camera_depth(self):
        positions, _uvs, _indices, _atlas = _scene(("near", "far"))
        depths = vertex_depths(positions, 0)
        self.assertAlmostEqual(depths[0], camera_depth((0.2 - 0.3, 0.2 + 0.3, 0.0), 0))

    def test_triangle_order_does_not_change_the_colour(self):
        first = rasterize(*_scene(("near", "far")), 0)
        second = rasterize(*_scene(("far", "near")), 0)
        self.assertTrue(np.array_equal(first, second))
        red = (first[..., 0] == 255) & (first[..., 3] == 255)
        blue = (first[..., 2] == 255) & (first[..., 3] == 255)
        self.assertGreater(np.count_nonzero(red), 0)
        self.assertGreater(np.count_nonzero(blue), 0)
        # The near card covers the overlap: no blue pixel lies inside the red card's box.
        ys, xs = np.nonzero(red)
        box = blue[ys.min() : ys.max() + 1, xs.min() : xs.max() + 1]
        self.assertEqual(np.count_nonzero(box), 0)

    def test_coverage_is_the_union_regardless_of_depth(self):
        both = rasterize(*_scene(("far", "near")), 0)
        positions, uvs, _indices, atlas = _scene(("near", "far"))
        near_only = rasterize(positions, uvs, np.array([0, 1, 2, 0, 2, 3]), atlas, 0)
        far_only = rasterize(positions, uvs, np.array([4, 5, 6, 4, 6, 7]), atlas, 0)
        union = (near_only[..., 3] > 0) | (far_only[..., 3] > 0)
        self.assertTrue(np.array_equal(both[..., 3] > 0, union))

    def test_holes_do_not_occlude(self):
        positions, uvs, indices, atlas = _scene(("near", "far"))
        atlas[:, :2, 3] = 0  # the near card is all MASK hole
        image = rasterize(positions, uvs, indices, atlas, 0)
        far_only = rasterize(positions, uvs, np.array([4, 5, 6, 4, 6, 7]), atlas, 0)
        self.assertTrue(np.array_equal(image, far_only))

    def test_untextured_path_is_depth_tested_too(self):
        positions, _uvs, indices, _atlas = _scene(("far", "near"))
        image = rasterize(positions, None, indices, None, 0)
        self.assertTrue(np.all(image[image[..., 3] > 0][:, :3] == 180))


if __name__ == "__main__":
    unittest.main()
