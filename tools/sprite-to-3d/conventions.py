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

# Checklist numbers. validate.py reads these and does not restate them.
# The Corsican Pine (864 tris, 1908 vertices, 256²) is the reference.
# A 256² atlas is about 256 KB of RGBA8 VRAM before the driver pads it
# (roughly 328 KB with the usual GPU alignment), which matters at ~10k models.
PINE_OBJECT_ID = "rct2.scenery_small.tcrp"
MAX_TRIANGLES = 1500
TREE_TRIANGLE_TARGET = (600, 1200)
MAX_VERTICES = 2500
MAX_TEXTURE = 256
PREFERRED_TEXTURE = 128
MAX_GLB_BYTES = 128 * 1024
TARGET_GLB_BYTES = 100 * 1024
PROP_FOOTPRINT = 0.5
TREE_FOOTPRINT = 0.85
HEIGHT_TOLERANCE_SIM = 2.0
BASE_SINK_MAX = 0.1
IOU_MIN_PER_VIEW = 0.65
IOU_MIN_MEAN = 0.70
AREA_RATIO = (0.9, 1.3)
TEXEL_DENSITY = 32.0
TEXEL_DENSITY_TOLERANCE = 0.25

# hard_tris is an error. max_texture is an error. preferred_texture is the
# size new models should use; a larger atlas warns unless the meta carries
# a written justification. The pine object id is the 256² reference and
# does not warn. canopy marks the classes allowed out to TREE_FOOTPRINT.
# tree_warn_below is the triangle count under which a tree class warns.
# Reasons for anything above a small prop live in the style guide.
SIZE_CLASSES = {
    "prop_quarter": {
        "soft_tris": 250,
        "hard_tris": 500,
        "max_texture": 128,
        "canopy": False,
        "tree_warn_below": None,
    },
    "prop_tile": {
        "soft_tris": 500,
        "hard_tris": 1000,
        "max_texture": 128,
        "canopy": False,
        "tree_warn_below": None,
    },
    "tree_small": {
        "soft_tris": 600,
        "hard_tris": 1200,
        "max_texture": 128,
        "canopy": True,
        "tree_warn_below": TREE_TRIANGLE_TARGET[0],
    },
    "tree_1tile": {
        "soft_tris": TREE_TRIANGLE_TARGET[0],
        "hard_tris": MAX_TRIANGLES,
        "max_texture": MAX_TEXTURE,
        "canopy": True,
        "tree_warn_below": TREE_TRIANGLE_TARGET[0],
    },
    "tree_large": {
        "soft_tris": TREE_TRIANGLE_TARGET[0],
        "hard_tris": MAX_TRIANGLES,
        "max_texture": MAX_TEXTURE,
        "canopy": True,
        "tree_warn_below": TREE_TRIANGLE_TARGET[0],
    },
    "large_scenery": {
        "soft_tris": TREE_TRIANGLE_TARGET[0],
        "hard_tris": MAX_TRIANGLES,
        "max_texture": MAX_TEXTURE,
        "canopy": False,
        "tree_warn_below": None,
    },
}

# How far below z = 0 a trunk may sink, in sim units. The checklist cap
# in Blender units is BASE_SINK_MAX (0.1), about 3 sim units.
DEFAULT_SINK_SIM = 3.0

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
