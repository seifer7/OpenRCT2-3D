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
# Quarter-tile objects are authored about their own quadrant centre. A
# quadrant is 0.5 x 0.5 tiles, so "inside the quadrant" is +-0.25 units.
QUARTER_FOOTPRINT = 0.25
# tree_quarter canopies may overhang their quadrant; the trunk base may not.
TREE_QUARTER_CANOPY = 0.40
# The trunk base is the bottom slice of the mesh: everything from the lowest
# vertex up to this many sim units above it (1/8 tile, a quarter land step).
# Triangle edges that cross the top of the band are cut at the band.
TRUNK_BASE_BAND_SIM = 4.0
HEIGHT_TOLERANCE_SIM = 2.0
# Per-view tip deltas (render tip minus that view's sprite tip, in sim units).
# Over TIP_VIEW_WARN_SIM warns; over TIP_VIEW_FAIL_SIM fails. The tallest-tip
# check above (HEIGHT_TOLERANCE_SIM) still applies on its own.
TIP_VIEW_WARN_SIM = 2.0
TIP_VIEW_FAIL_SIM = 4.0
BASE_SINK_MAX = 0.1
# Raw-mask IoU reference levels. Reported and recorded in the meta as
# iou.threshold_per_view / iou.threshold_mean; they no longer gate.
IOU_MIN_PER_VIEW = 0.65
IOU_MIN_MEAN = 0.70
# The IoU gate (batch 1 QA decision 1). Both the sprite mask and the model
# mask get a morphological close with a square IOU_CLOSE_KERNEL x
# IOU_CLOSE_KERNEL structuring element (3 = 3x3, "3px") before scoring, so a
# stippled sprite is not scored pixel by pixel. The thresholds are tighter
# than the raw ones because the close inflates every score.
IOU_CLOSE_KERNEL = 3
IOU_CLOSED_MIN_PER_VIEW = 0.70
IOU_CLOSED_MIN_MEAN = 0.75
AREA_RATIO = (0.9, 1.3)
TEXEL_DENSITY = 32.0
TEXEL_DENSITY_TOLERANCE = 0.25
# Palette-index histograms in params.json (sprite_hist, top_hist, bark_hist,
# hist, rim_hist, ...) are counts derived locally from the extracted sprite;
# no pixel data is stored. Each one may hold at most this many entries, and
# rebuild.py does not copy them into the meta.
MAX_HIST_ENTRIES = 16
# Opaque-pixel floor per view for classes not in SIZE_CLASSES.
DEFAULT_MIN_RENDER_PX = 500

# Per-class budgets. validate.py reads every number from here.
#   hard_tris        over this fails.
#   soft_max_tris    over this warns (exit code unaffected). A meta
#                    triangle_justification annotates the warning; it
#                    does not remove it.
#   tree_warn_below  under this warns (tree classes only; props may be cheap).
#   max_texture      atlas cap, an error. PREFERRED_TEXTURE (128) is the size
#                    new models use; a larger atlas warns unless the meta
#                    carries a written justification. The pine object id is
#                    the 256² reference and does not warn.
#   min_render_px    opaque-pixel floor per comparison view, a guard against
#                    an empty or broken render (the area ratio is the real
#                    guard when sprites are present).
#   footprint_tiles  the tile area the class occupies.
#   footprint_bu     max |x| and |y| of the whole mesh, in tile units.
#   trunk_base_bu    max |x| and |y| of the trunk base (the bottom
#                    TRUNK_BASE_BAND_SIM of the mesh), or None for no check.
#   canopy           True for the classes allowed out to TREE_FOOTPRINT.
# Reasons for anything above a small prop live in the style guide.
SIZE_CLASSES = {
    "prop_quarter": {
        "soft_max_tris": 250,
        "hard_tris": 500,
        "tree_warn_below": None,
        "max_texture": 128,
        "min_render_px": 100,
        "footprint_tiles": (0.5, 0.5),
        "footprint_bu": QUARTER_FOOTPRINT,
        "trunk_base_bu": None,
        "canopy": False,
    },
    "prop_tile": {
        "soft_max_tris": 500,
        "hard_tris": 1000,
        "tree_warn_below": None,
        "max_texture": 128,
        "min_render_px": 250,
        "footprint_tiles": (1, 1),
        "footprint_bu": PROP_FOOTPRINT,
        "trunk_base_bu": None,
        "canopy": False,
    },
    "tree_quarter": {
        "soft_max_tris": 400,
        "hard_tris": 600,
        "tree_warn_below": 200,
        "max_texture": 128,
        "min_render_px": 150,
        "footprint_tiles": (0.5, 0.5),
        "footprint_bu": TREE_QUARTER_CANOPY,
        "trunk_base_bu": QUARTER_FOOTPRINT,
        "canopy": False,
    },
    "tree_small": {
        "soft_max_tris": 1000,
        "hard_tris": 1200,
        "tree_warn_below": TREE_TRIANGLE_TARGET[0],
        "max_texture": 128,
        "min_render_px": DEFAULT_MIN_RENDER_PX,
        "footprint_tiles": (1, 1),
        "footprint_bu": TREE_FOOTPRINT,
        "trunk_base_bu": None,
        "canopy": True,
    },
    "tree_1tile": {
        "soft_max_tris": TREE_TRIANGLE_TARGET[1],
        "hard_tris": MAX_TRIANGLES,
        "tree_warn_below": TREE_TRIANGLE_TARGET[0],
        "max_texture": MAX_TEXTURE,
        "min_render_px": DEFAULT_MIN_RENDER_PX,
        "footprint_tiles": (1, 1),
        "footprint_bu": TREE_FOOTPRINT,
        "trunk_base_bu": None,
        "canopy": True,
    },
    "tree_large": {
        "soft_max_tris": TREE_TRIANGLE_TARGET[1],
        "hard_tris": MAX_TRIANGLES,
        "tree_warn_below": TREE_TRIANGLE_TARGET[0],
        "max_texture": MAX_TEXTURE,
        "min_render_px": DEFAULT_MIN_RENDER_PX,
        "footprint_tiles": (1, 1),
        "footprint_bu": TREE_FOOTPRINT,
        "trunk_base_bu": None,
        "canopy": True,
    },
    "large_scenery": {
        "soft_max_tris": 1200,
        "hard_tris": MAX_TRIANGLES,
        "tree_warn_below": None,
        "max_texture": MAX_TEXTURE,
        "min_render_px": DEFAULT_MIN_RENDER_PX,
        "footprint_tiles": (1, 1),
        "footprint_bu": PROP_FOOTPRINT,
        "trunk_base_bu": None,
        "canopy": False,
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
