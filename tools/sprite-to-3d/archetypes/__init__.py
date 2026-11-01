# Copyright (c) 2026 OpenRCT2 developers
# Licensed under the GNU General Public License version 3.

"""Mesh archetypes. Each one turns a params dict and an atlas tile map into a mesh."""

from __future__ import annotations


def triangle_count(mesh):
    """Triangles in a built mesh (any archetype): an n-gon face is n - 2 triangles."""
    return sum(len(face) - 2 for face in mesh.F)


def build_mesh(archetype, params, tiles):
    if archetype == "conifer":
        from . import conifer

        return conifer.build(params, tiles)
    if archetype == "prop":
        from . import prop

        return prop.build(params, tiles)
    raise ValueError(
        f"unknown archetype {archetype!r}. Implemented: conifer, prop "
        "(the Corsican Pine template). Add a module beside conifer.py for a new shape."
    )


def write_atlas(archetype, params, palette, png_path, json_path):
    """Paint the archetype's atlas. Returns {"size", "tiles", ...}."""
    if archetype == "conifer":
        from .conifer_atlas import write_atlas as conifer_write_atlas

        return conifer_write_atlas(params, palette, png_path, json_path)
    if archetype == "prop":
        from .prop_atlas import write_atlas as prop_write_atlas

        return prop_write_atlas(params, palette, png_path, json_path)
    raise ValueError(f"unknown archetype {archetype!r}. Implemented: conifer, prop")
