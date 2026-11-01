# Copyright (c) 2026 OpenRCT2 developers
# Licensed under the GNU General Public License version 3.

"""Authoring constants shared by the build, the exporter, and the validator.

These match docs/3d-viewer.md, docs/agents/glb-model-pipeline-recommendation.md,
and ParkView.cpp: 1 Blender unit = 1 tile = 32 sim units on every axis.
"""

from __future__ import annotations

import math

TOOL_VERSION = "sprite-to-3d 1"
SCHEMA = "openrct2-3d.object-model.v1"

# 1 Blender unit = 1 tile = 32 sim units on x, y, and z.
SIM_PER_UNIT = 32.0
# Render-only. sqrt(2/3). Never applied to an exported GLB.
Z_SQUASH = math.sqrt(2.0 / 3.0)
# Comparison renders. Origin of the model lands on this pixel.
RENDER_SIZE = (128, 192)
ORIGIN_PIXEL = (64.0, 160.0)

# glTF sampler enums.
NEAREST = 9728
CLAMP_TO_EDGE = 33071
MIPMAP_MIN_FILTERS = {9984, 9985, 9986, 9987}  # NEAREST_MIPMAP_*, LINEAR_MIPMAP_*

# IoU against the placed sprite mask. The approved pine clears these
# (worst view 0.687, mean 0.725) with a little room.
IOU_MIN_PER_VIEW = 0.65
IOU_MIN_MEAN = 0.70

# Poly and texture budgets. The Corsican Pine (864 tris, 256^2) is the
# reference point for tree_1tile. Hard caps are what the validator enforces.
# Soft caps are the art-direction target documented in the style guide.
SIZE_CLASSES = {
    "prop_quarter": {"soft_tris": 250, "hard_tris": 500, "max_texture": 128},
    "prop_tile": {"soft_tris": 500, "hard_tris": 1000, "max_texture": 256},
    "tree_small": {"soft_tris": 800, "hard_tris": 1200, "max_texture": 256},
    "tree_1tile": {"soft_tris": 1200, "hard_tris": 1500, "max_texture": 256},
    "tree_large": {"soft_tris": 2500, "hard_tris": 4000, "max_texture": 512},
    "large_scenery": {"soft_tris": 4000, "hard_tris": 8000, "max_texture": 512},
}

# How far below z = 0 a trunk may sink so it meets uneven land, in sim units.
DEFAULT_SINK_SIM = 8.0

# Recolour palette index ranges (PaletteIndex.h).
RECOLOUR_RANGES = {
    "primary": (243, 254),
    "secondary": (202, 213),
    "tertiary": (46, 57),
}

META_REQUIRED = (
    "schema",
    "tool_version",
    "object_id",
    "dat_name",
    "display_name",
    "archetype",
    "size_class",
    "source_sprites",
    "units",
    "bounds_blender",
    "height_sim",
    "footprint_tiles",
    "max_overhang_tiles",
    "base_sink_sim",
    "triangles",
    "texture",
    "iou",
    "params",
    "review",
)


def size_class(name):
    if name not in SIZE_CLASSES:
        known = ", ".join(sorted(SIZE_CLASSES))
        raise KeyError(f"unknown size class {name!r} (expected one of: {known})")
    return SIZE_CLASSES[name]
