"""Opt-in archetype options leave objects that don't use them unchanged."""

import copy
import json
import os
import unittest

import numpy as np

from archetypes import conifer, tree_atlas

HERE = os.path.dirname(os.path.abspath(__file__))
PALETTE = np.array([(i, (i * 7) % 256, (i * 13) % 256) for i in range(256)], np.uint8)


def _params(object_id):
    with open(os.path.join(HERE, "examples", object_id, "params.json")) as f:
        return json.load(f)


class PinholeTest(unittest.TestCase):
    def setUp(self):
        self.params = _params("rct2.scenery_small.tcc")
        self.params["atlas"].pop("pinholes", None)

    def test_zero_pinholes_is_a_no_op(self):
        rgba, _, _ = tree_atlas.paint(self.params, PALETTE)
        zero = copy.deepcopy(self.params)
        zero["atlas"]["pinholes"] = 0.0
        self.assertTrue(np.array_equal(rgba, tree_atlas.paint(zero, PALETTE)[0]))

    def test_pinholes_clear_only_pad_texels(self):
        _, tiles, base = tree_atlas.paint(self.params, PALETTE)
        holed = copy.deepcopy(self.params)
        holed["atlas"]["pinholes"] = 0.3
        _, _, idx = tree_atlas.paint(holed, PALETTE)
        self.assertTrue(np.all((idx == base) | (idx == 0)))
        for name, (x, y, w, h) in tiles.items():
            a, b = base[y : y + h, x : x + w], idx[y : y + h, x : x + w]
            if name.startswith(("pad_top", "pad_side")):
                self.assertLess(np.count_nonzero(b), np.count_nonzero(a))
            else:
                self.assertTrue(np.array_equal(a, b), name)


class LimbReachTest(unittest.TestCase):
    def _mesh(self, params):
        _, tiles, _ = tree_atlas.paint(params, PALETTE)
        return conifer.build(params, tiles)

    def test_default_reach_matches_explicit_0_6(self):
        params = _params("rct2.scenery_small.tcc")
        params["quality"].pop("limb_reach", None)
        explicit = copy.deepcopy(params)
        explicit["quality"]["limb_reach"] = 0.6
        self.assertEqual(self._mesh(params).V, self._mesh(explicit).V)

    def test_zero_reach_shortens_limbs(self):
        params = _params("rct2.scenery_small.tcc")
        params["quality"]["limb_reach"] = 0.6
        short = copy.deepcopy(params)
        short["quality"]["limb_reach"] = 0.0
        span = lambda m: max(np.hypot(x, y) for x, y, _ in m.V)
        self.assertLessEqual(span(self._mesh(short)), span(self._mesh(params)))


if __name__ == "__main__":
    unittest.main()
