# Copyright (c) 2026 OpenRCT2 developers
# Licensed under the GNU General Public License version 3.

"""Tip height is measured up from the anchor row, not from the mask bottom."""

from __future__ import annotations

import unittest

import numpy as np

import conventions as C
from validate import compare_tip_heights, sprite_anchor_y, tip_height_sim


def _column(top, bottom, width=128, height=192):
    mask = np.zeros((height, width), dtype=bool)
    mask[top : bottom + 1, width // 2] = True
    return mask


class TipHeightTest(unittest.TestCase):
    def test_sink_below_the_anchor_does_not_count(self):
        # Opaque from row 26 through 165. Anchor is 160, so the tip is 134
        # and the five rows below the anchor are the sunk trunk.
        mask = _column(26, 165)
        self.assertEqual(tip_height_sim(mask, 160), 134)
        self.assertNotEqual(tip_height_sim(mask, 160), 165 - 26)

    def test_pine_tips_match_the_tallest_sprite(self):
        # Measured on the approved pine with alignment [-2, 0] and nudge -1:
        # sprite tops 25, 29, 29, 28 against anchor row 159, render top 26
        # against anchor row 160. The tallest sprite tip is view 0.
        shift = [-2, 0]
        nudge = -1
        self.assertEqual(sprite_anchor_y(shift, nudge), 159)
        sprite_tops = (25, 29, 29, 28)
        sprites = [_column(top, 163) for top in sprite_tops]
        renders = [_column(26, 165) for _ in range(4)]
        ok, model_tip, tallest, rows = compare_tip_heights(renders, sprites, shift, nudge)
        self.assertTrue(ok)
        self.assertEqual(model_tip, 134)
        self.assertEqual(tallest, 134)
        self.assertEqual([round(delta) for _sprite, _render, delta in rows], [0, 4, 4, 3])
        self.assertLessEqual(abs(model_tip - tallest), C.HEIGHT_TOLERANCE_SIM)

    def test_a_tip_more_than_two_sim_off_fails(self):
        shift = [-2, 0]
        nudge = -1
        sprites = [_column(25, 163) for _ in range(4)]
        renders = [_column(20, 165) for _ in range(4)]
        ok, model_tip, tallest, _rows = compare_tip_heights(renders, sprites, shift, nudge)
        self.assertFalse(ok)
        self.assertGreater(abs(model_tip - tallest), C.HEIGHT_TOLERANCE_SIM)


if __name__ == "__main__":
    unittest.main()
