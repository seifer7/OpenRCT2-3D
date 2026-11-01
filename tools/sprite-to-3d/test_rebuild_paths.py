# Copyright (c) 2026 OpenRCT2 developers
# Licensed under the GNU General Public License version 3.

"""rebuild.py sends the review sheet to --out and counts triangles archetype-neutrally."""

from __future__ import annotations

import os
import inspect
import unittest

import archetypes
import rebuild


class ReviewPathTest(unittest.TestCase):
    def test_review_png_goes_to_out(self):
        self.assertEqual(rebuild.review_png_path("/tmp/scratch", "rct2.x"), os.path.join("/tmp/scratch", "rct2.x.review.png"))

    def test_validator_command_writes_the_review_into_out(self):
        out = os.path.join("scratch", "dir")
        cmd = rebuild.validator_command(
            os.path.join(out, "a.glb"), os.path.join(out, "a.meta.json"), rebuild.review_png_path(out, "a"), "sprites"
        )
        self.assertEqual(cmd[cmd.index("--out") + 1], os.path.join(out, "a.review.png"))
        self.assertEqual(cmd[-3:], ["--sprites", "sprites", "--require-iou"])
        self.assertNotIn("--sprites", rebuild.validator_command("g", "m", "r", None))

    def test_main_uses_review_png_path_with_out_dir(self):
        # main() builds the review path from --out (out_dir), not from TOOL_DIR/out.
        src = inspect.getsource(rebuild.main)
        self.assertIn('review_png_path(out_dir, spec["object_id"])', src)
        self.assertNotIn(".review.png", src)


class TriangleCountTest(unittest.TestCase):
    def test_rebuild_uses_the_neutral_helper(self):
        self.assertIs(rebuild.triangle_count, archetypes.triangle_count)


if __name__ == "__main__":
    unittest.main()
